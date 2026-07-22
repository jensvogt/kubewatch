#pragma once

#include <QDateTime>
#include <QString>

#include <functional>

// Temporary AWS credentials obtained via AssumeRoleWithSAML.
struct AwsSessionCredentials {
    QString accessKeyId;
    QString secretAccessKey;
    QString sessionToken;
    QDateTime expiresAt;

    [[nodiscard]] bool isValid() const {
        return !accessKeyId.isEmpty() && QDateTime::currentDateTimeUtc() < expiresAt;
    }
};

// Performs a full OneLogin SAML login (OAuth token -> SAML assertion -> MFA verify)
// followed by AWS STS AssumeRoleWithSAML (via the `aws` CLI), returning session
// credentials for the given OneLogin AWS-connector app. On failure, returns an
// invalid AwsSessionCredentials and sets *error.
//
// promptForOtpToken is called (on the calling/GUI thread) only once the MFA challenge
// has actually been received from OneLogin, immediately before it's needed -- TOTP
// codes are only valid for ~30s, so asking for it any earlier risks it expiring during
// the preceding network round-trips. Return an empty string to cancel the login.
AwsSessionCredentials LoginWithOneLogin(const QString& username, const QString& password,
                                        const std::function<QString()>& promptForOtpToken, long appId,
                                        QString* error);
