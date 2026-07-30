#include <kubectl/KubeNetService.h>
#include <kubectl/KubectlClient.h>

#include <QEventLoop>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QUrl>

#include <utils/Logging.h>

KubeNetService::KubeNetService(const QString &context) : context_(context) {
    // --minify --flatten collapses the view down to just this context's cluster/user
    // entries, with certificate-authority-data embedded inline (rather than a path) --
    // exactly what's needed here, in one call.
    const KubectlResult viewResult = KubectlClient::runKubectlCommand(
            {"--kubeconfig", KubectlClient::Kubeconfig(), "config", "view", "--minify", "--flatten", "--context", context, "-o", "json"});
    if (!viewResult.success) {
        lastError_ = "Failed to read kubeconfig: " + viewResult.error;
        return;
    }

    const QJsonObject config = QJsonDocument::fromJson(viewResult.output.toUtf8()).object();
    const QJsonArray clusters = config["clusters"].toArray();
    const QJsonArray users = config["users"].toArray();
    if (clusters.isEmpty() || users.isEmpty()) {
        lastError_ = "kubeconfig context '" + context + "' has no cluster/user entry.";
        return;
    }

    const QJsonObject cluster = clusters.first().toObject()["cluster"].toObject();
    server_ = cluster["server"].toString();
    caCertPem_ = QByteArray::fromBase64(cluster["certificate-authority-data"].toString().toUtf8());

    // EKS exec plugins (`aws`/`aws-iam-authenticator`) receive the cluster's short name
    // as `--cluster-name <name>` among their exec args; pull it out from there rather
    // than assuming it matches the kubeconfig cluster entry's own name (often the ARN).
    const QJsonArray execArgs = users.first().toObject()["user"].toObject()["exec"].toObject()["args"].toArray();
    for (int i = 0; i + 1 < execArgs.size(); ++i) {
        if (execArgs[i].toString() == "--cluster-name") {
            clusterName_ = execArgs[i + 1].toString();
            break;
        }
    }

    if (server_.isEmpty() || caCertPem_.isEmpty()) {
        lastError_ = "kubeconfig context '" + context + "' is missing server/CA data.";
    } else if (clusterName_.isEmpty()) {
        lastError_ = "Could not determine EKS cluster name from context '" + context + "' (non-EKS or non-exec auth context?).";
    }
}

QString KubeNetService::ResourcePath(const QString &resource, const QString &ns) {
    // The API group/version each resource kind is served under. Resources not listed
    // here (pods, services, events, endpoints, resourcequotas, limitranges, configmaps,
    // secrets, persistentvolumeclaims, namespaces, nodes) are core/v1, i.e. "/api/v1".
    static const QHash<QString, QString> groupVersionPrefixes = {
            {"deployments", "/apis/apps/v1"},
            {"replicasets", "/apis/apps/v1"},
            {"daemonsets", "/apis/apps/v1"},
            {"statefulsets", "/apis/apps/v1"},
            {"jobs", "/apis/batch/v1"},
            {"ingresses", "/apis/networking.k8s.io/v1"},
            {"horizontalpodautoscalers", "/apis/autoscaling/v2"},
    };
    const QString prefix = groupVersionPrefixes.value(resource, "/api/v1");
    if (ns.isEmpty() || ns == "All namespaces") return prefix + "/" + resource;
    return prefix + "/namespaces/" + ns + "/" + resource;
}

KubeNetService &KubeNetService::forContext(const QString &context) {
    static QHash<QString, KubeNetService> cache;
    auto it = cache.find(context);
    if (it == cache.end() || !it->IsValid()) {
        it = cache.insert(context, KubeNetService(context));
    }
    return it.value();
}

QString KubeNetService::bearerToken() {
    QString error;
    const QString token = KubectlClient::GetBearerToken(clusterName_, &error);
    if (token.isEmpty()) {
        lastError_ = error;
    }
    return token;
}

QByteArray KubeNetService::httpGet(const QString &apiPath, QString *error) {
    const QString token = bearerToken();
    if (token.isEmpty()) {
        if (error) *error = lastError_;
        return {};
    }

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(server_ + apiPath)};
    request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    request.setRawHeader("Accept", "application/json");

    // Pin the cluster's own CA instead of trusting the system store.
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setCaCertificates({QSslCertificate(caCertPem_, QSsl::Pem)});
    request.setSslConfiguration(sslConfig);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const bool hadNetworkError = reply->error() != QNetworkReply::NoError;
    const QString networkErrorString = reply->errorString();
    reply->deleteLater();

    if (hadNetworkError || status != 200) {
        if (error) {
            // On a real request that reached the API server, the body is a Kubernetes
            // Status object (e.g. {"status":"Failure","reason":"Forbidden",...}).
            *error = status != 0 ? QString("Kubernetes API returned HTTP %1: %2").arg(status).arg(QString::fromUtf8(body))
                                  : networkErrorString;
        }
        return {};
    }
    return body;
}

QJsonArray KubeNetService::fetchItems(const QString &apiPath) {
    QString error;
    const QByteArray body = httpGet(apiPath, &error);
    if (body.isEmpty()) {
        lastError_ = error;
        logWarning << "KubeNetService: GET" << apiPath << "failed:" << error;
        return {};
    }
    return QJsonDocument::fromJson(body).object()["items"].toArray();
}

QJsonArray KubeNetService::fetchNodes() {
    return fetchItems("/api/v1/nodes");
}

QJsonObject KubeNetService::fetchObject(const QString &apiPath) {
    QString error;
    const QByteArray body = httpGet(apiPath, &error);
    if (body.isEmpty()) {
        lastError_ = error;
        logWarning << "KubeNetService: GET" << apiPath << "failed:" << error;
        return {};
    }
    return QJsonDocument::fromJson(body).object();
}

QByteArray KubeNetService::fetchRaw(const QString &apiPath, QString *error) {
    QString err;
    const QByteArray body = httpGet(apiPath, &err);
    if (body.isEmpty() && !err.isEmpty()) {
        lastError_ = err;
        logWarning << "KubeNetService: GET" << apiPath << "failed:" << err;
    }
    if (error) *error = err;
    return body;
}
