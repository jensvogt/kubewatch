#pragma once

#include <QString>

class QWidget;

class OtpDialog {
public:
    // Shows a small dialog asking only for the authenticator token, and returns what
    // was typed (or an empty string if cancelled). Called by LoginDialog only once the
    // MFA challenge has actually arrived, so the code is submitted almost immediately
    // after being entered rather than sitting around expiring.
    static QString Prompt(QWidget *parent);
};
