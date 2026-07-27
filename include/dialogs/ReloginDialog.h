#pragma once

#include <onelogin/OneLoginAuth.h>

#include <QString>

class QWidget;

class ReloginDialog {
public:
    struct Result {
        bool success = false;
        QString username;
        AwsSessionCredentials credentials;
    };

    // Shown when the cached AWS session for `accountKey` (obtained via OneLogin) has
    // expired and a kubectl call needs fresh credentials before it can proceed.
    // Unlike LoginDialog there's no context picker -- the context hasn't changed,
    // only its session has expired.
    static Result Show(QWidget *parent, const QString &accountKey, const QString &initialUsername);
};
