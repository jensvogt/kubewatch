#include <onelogin/OneLoginAuth.h>

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrl>
#include <QXmlStreamReader>

#include <utils/Configuration.h>
#include <utils/Logging.h>

namespace {
QByteArray httpPost(const QString& url, const QByteArray& body, const QMap<QString, QString>& headers,
                     int* statusOut, QString* networkErrorOut = nullptr) {
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    QNetworkReply* reply = manager.post(request, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (statusOut) {
        *statusOut = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    if (networkErrorOut && reply->error() != QNetworkReply::NoError) {
        *networkErrorOut = reply->errorString();
    }
    const QByteArray response = reply->readAll();
    reply->deleteLater();
    return response;
}

// Parses the base64-encoded SAML assertion for the AWS Role attribute, which holds
// "<roleArn>,<principalArn>" (order not guaranteed by the IdP configuration).
bool extractRoleAndPrincipalArn(const QString& base64SamlAssertion, QString* roleArn, QString* principalArn) {
    const QByteArray decoded = QByteArray::fromBase64(base64SamlAssertion.toUtf8());
    QXmlStreamReader reader(decoded);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QStringLiteral("Attribute")) continue;
        if (reader.attributes().value("Name") != QStringLiteral("https://aws.amazon.com/SAML/Attributes/Role")) {
            continue;
        }

        while (!reader.atEnd()) {
            reader.readNext();
            if (!reader.isStartElement() || reader.name() != QStringLiteral("AttributeValue")) continue;
            for (const QString& arn : reader.readElementText().split(',')) {
                if (arn.contains(":saml-provider/")) {
                    *principalArn = arn;
                } else if (arn.contains(":role/")) {
                    *roleArn = arn;
                }
            }
            return !roleArn->isEmpty() && !principalArn->isEmpty();
        }
    }
    return false;
}
}  // namespace

AwsSessionCredentials LoginWithOneLogin(const QString& username, const QString& password,
                                        const std::function<QString()>& promptForOtpToken, long appId,
                                        QString* error) {
    AwsSessionCredentials result;
    auto setError = [&](const QString& msg) {
        logError << msg;
        if (error) *error = msg;
    };

    const auto subDomain = Configuration::instance().GetValue<QString>("onelogin.subDomain", QString());
    const auto clientId = Configuration::instance().GetValue<QString>("onelogin.clientId", QString());
    const auto clientSecret = Configuration::instance().GetValue<QString>("onelogin.clientSecret", QString());
    if (subDomain.isEmpty() || clientId.isEmpty() || clientSecret.isEmpty()) {
        setError("OneLogin is not configured (onelogin.subDomain/clientId/clientSecret missing).");
        return result;
    }

    const QString baseUrl = "https://" + subDomain + ".onelogin.com";
    int status = 0;

    // Step 1: OAuth2 client-credentials token.
    logInfo << "OneLogin: requesting access token...";
    QMap<QString, QString> tokenHeaders;
    tokenHeaders["Authorization"] = "client_id:" + clientId + ",client_secret:" + clientSecret;
    tokenHeaders["Content-Type"] = "application/json";

    QJsonObject tokenRequest;
    tokenRequest["grant_type"] = "client_credentials";

    QString networkError;
    const QByteArray tokenResponseBytes = httpPost(baseUrl + "/auth/oauth2/v2/token", QJsonDocument(tokenRequest).toJson(),
                                                     tokenHeaders, &status, &networkError);
    const QJsonObject tokenResponse = QJsonDocument::fromJson(tokenResponseBytes).object();
    const QString accessToken = tokenResponse["access_token"].toString();
    if (accessToken.isEmpty()) {
        const QString detail = !networkError.isEmpty() ? networkError
                                : !tokenResponseBytes.isEmpty() ? QString::fromUtf8(tokenResponseBytes)
                                                                 : "no response body";
        setError(QString("OneLogin token request failed (HTTP %1): %2").arg(status).arg(detail));
        return result;
    }

    logInfo << "OneLogin: got access token, requesting SAML assertion for app" << appId << "...";

    // Step 2: SAML assertion request -- this OneLogin tenant always responds with an MFA challenge.
    QMap<QString, QString> samlHeaders;
    samlHeaders["Authorization"] = "bearer:" + accessToken;
    samlHeaders["Content-Type"] = "application/json";

    QJsonObject samlRequest;
    samlRequest["username_or_email"] = username;
    samlRequest["password"] = password;
    samlRequest["app_id"] = QString::number(appId);
    samlRequest["subdomain"] = subDomain;
    samlRequest["ip_address"] = "";

    const QByteArray samlResponseBytes =
        httpPost(baseUrl + "/api/2/saml_assertion", QJsonDocument(samlRequest).toJson(), samlHeaders, &status);
    const QJsonObject samlResponse = QJsonDocument::fromJson(samlResponseBytes).object();

    const QString stateToken = samlResponse["state_token"].toString();
    const QJsonArray devices = samlResponse["devices"].toArray();
    if (stateToken.isEmpty() || devices.isEmpty()) {
        const QString message = samlResponse["message"].toString();
        setError(QString("OneLogin SAML assertion failed (HTTP %1): %2")
                     .arg(status)
                     .arg(message.isEmpty() ? QString::fromUtf8(samlResponseBytes) : message));
        return result;
    }
    // onelogin.deviceId is the *index* into the devices array the SAML assertion
    // offered (0 = first device), not OneLogin's own opaque device_id value.
    const int deviceIndex = Configuration::instance().GetValue<int>("onelogin.deviceId", 0);
    const int clampedIndex = (deviceIndex >= 0 && deviceIndex < devices.size()) ? deviceIndex : 0;
    if (clampedIndex != deviceIndex) {
        logWarning << "OneLogin: configured onelogin.deviceId index" << deviceIndex << "is out of range (only"
                   << devices.size() << "device(s) offered), falling back to the first device.";
    }
    const int deviceId = devices[clampedIndex].toObject()["device_id"].toInt();
    logInfo << "OneLogin: MFA challenge received, prompting for authenticator token for device" << deviceId << "...";

    // Ask for the OTP token only now, right before it's needed -- TOTP codes expire in
    // ~30s, and steps 1-2 above already ate into that window.
    const QString otpToken = promptForOtpToken();
    if (otpToken.isEmpty()) {
        setError("Login cancelled.");
        return result;
    }

    // Step 3: verify the authenticator token the user just typed in.
    QJsonObject verifyRequest;
    verifyRequest["app_id"] = QString::number(appId);
    verifyRequest["otp_token"] = otpToken;
    verifyRequest["device_id"] = QString::number(deviceId);
    verifyRequest["state_token"] = stateToken;

    const QByteArray verifyResponseBytes = httpPost(baseUrl + "/api/2/saml_assertion/verify_factor",
                                                      QJsonDocument(verifyRequest).toJson(), samlHeaders, &status);
    const QJsonObject verifyResponse = QJsonDocument::fromJson(verifyResponseBytes).object();
    const QString samlAssertion = verifyResponse["data"].toString();
    if (samlAssertion.isEmpty()) {
        const QString message = verifyResponse["message"].toString();
        setError(QString("OneLogin MFA verification failed (HTTP %1): %2")
                     .arg(status)
                     .arg(message.isEmpty() ? QString::fromUtf8(verifyResponseBytes) : message));
        return result;
    }

    QString roleArn;
    QString principalArn;
    if (!extractRoleAndPrincipalArn(samlAssertion, &roleArn, &principalArn)) {
        setError("Could not find AWS role/principal ARN in SAML assertion.");
        return result;
    }
    logInfo << "OneLogin: MFA verified, assuming role" << roleArn << "via AWS STS...";

    // Exchange the SAML assertion for temporary AWS credentials via the AWS CLI.
    const int durationSeconds = Configuration::instance().GetValue<int>("aws.session-duration-seconds", 3600);
    QProcess process;
    process.start("aws", {"sts", "assume-role-with-saml", "--role-arn", roleArn, "--principal-arn", principalArn,
                           "--saml-assertion", samlAssertion, "--duration-seconds", QString::number(durationSeconds),
                           "--output", "json"});
    if (!process.waitForFinished(30000)) {
        setError("aws sts assume-role-with-saml timed out.");
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError("aws sts assume-role-with-saml failed: " + QString::fromLocal8Bit(process.readAllStandardError()));
        return result;
    }

    const QJsonObject stsResult = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    const QJsonObject credentials = stsResult["Credentials"].toObject();
    result.accessKeyId = credentials["AccessKeyId"].toString();
    result.secretAccessKey = credentials["SecretAccessKey"].toString();
    result.sessionToken = credentials["SessionToken"].toString();
    result.expiresAt = QDateTime::fromString(credentials["Expiration"].toString(), Qt::ISODate);
    if (!result.isValid()) {
        setError("aws sts assume-role-with-saml returned no usable credentials.");
    } else {
        logInfo << "OneLogin: login succeeded, session valid until" << result.expiresAt.toString(Qt::ISODate);
    }
    return result;
}
