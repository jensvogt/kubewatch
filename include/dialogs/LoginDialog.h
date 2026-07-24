#pragma once

#include <onelogin/OneLoginAuth.h>

#include <QString>
#include <QStringList>

class QWidget;

class LoginDialog {
public:
    struct Result {
        bool success = false;
        // Set whenever the dialog was accepted with a non-empty context selected,
        // even if login itself then failed -- MainWindow switches its context combo
        // to match either way, same as before the refactor.
        QString selectedContext;
        QString username;
        QString accountKey;
        AwsSessionCredentials credentials;
    };

    // Shows the OneLogin sign-in dialog (context/username/password), performs the
    // login on acceptance (prompting for the OTP token via OtpDialog once the MFA
    // challenge arrives), and reports the outcome. Shows its own QMessageBox
    // warnings for cancellation/validation/login failures.
    static Result Show(QWidget *parent, const QStringList &contexts, const QString &currentContext,
                        const QString &initialUsername);
};
