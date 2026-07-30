#pragma once

// Qt includes
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// Talks to a Kubernetes cluster's REST API directly over HTTPS, bypassing kubectl for
// the actual data calls (GET/list, including pod logs) -- every table and detail dialog's
// reads go through this. Only two subprocesses are still involved: a one-time `kubectl
// config view` to resolve the current context's server/CA/cluster name, and an `aws eks
// get-token` call (via KubectlClient::GetBearerToken) each time a fresh bearer token is
// needed. Mutations (apply/delete) still go through KubectlClient directly.
class KubeNetService {
public:
    // Resolves the given kubeconfig context's cluster server/CA and EKS cluster name.
    // Check IsValid() after construction; on failure, LastError() explains why.
    explicit KubeNetService(const QString &context);

    // Returns a cached instance for the given context, running `kubectl config view`
    // only once per context (retried if the cached instance isn't valid) rather than on
    // every call site's construction.
    static KubeNetService &forContext(const QString &context);

    [[nodiscard]] bool IsValid() const { return !server_.isEmpty() && !caCertPem_.isEmpty() && !clusterName_.isEmpty(); }
    [[nodiscard]] const QString &LastError() const { return lastError_; }

    // Maps a plural resource name (e.g. "pods", "deployments", "ingresses") to its REST
    // API list path, prefixed with the right API group/version and (unless `ns` is empty
    // or "All namespaces", matching the namespace combo box's sentinel) scoped to that
    // namespace. Callers append "/<name>" themselves for a single-object GET, or a "?"
    // query string for field/label selectors.
    static QString ResourcePath(const QString &resource, const QString &ns = QString());

    // GET /api/v1/nodes -- returns the NodeList's "items" array.
    QJsonArray fetchNodes();

    // Generic GET against an arbitrary Kubernetes API path (e.g. "/api/v1/pods",
    // "/apis/apps/v1/deployments"), returning the response's "items" array. Empty on
    // failure -- check LastError() to distinguish that from a genuinely empty list.
    QJsonArray fetchItems(const QString &apiPath);

    // Generic GET of a single object at an arbitrary Kubernetes API path (e.g.
    // "/api/v1/namespaces/default/pods/my-pod"). Empty on failure -- check LastError().
    QJsonObject fetchObject(const QString &apiPath);

    // Generic GET returning the raw response body rather than parsed JSON -- e.g. a pod's
    // "/log" subresource, which is plain text and can legitimately be empty (no failure).
    // If `error` is given, it's set to a non-empty message only on a genuine failure --
    // check that rather than the returned body's emptiness, and instead of LastError(),
    // which a prior successful call on this same instance won't have cleared.
    QByteArray fetchRaw(const QString &apiPath, QString *error = nullptr);

private:
    QString bearerToken();
    QByteArray httpGet(const QString &apiPath, QString *error);

    QString context_;
    QString server_;
    QByteArray caCertPem_;
    QString clusterName_;
    QString lastError_;
};
