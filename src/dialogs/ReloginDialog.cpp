#include <dialogs/ReloginDialog.h>

#include <dialogs/OtpDialog.h>
#include <kubectl/KubectlClient.h>
#include <utils/Configuration.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

ReloginDialog::Result ReloginDialog::Show(QWidget *parent, const QString &accountKey, const QString &initialUsername) {
    Result result;

    QDialog dialog(parent);
    dialog.setWindowTitle("Session Expired");
    dialog.setMinimumWidth(450);

    auto *form = new QFormLayout(&dialog);

    auto *messageLabel = new QLabel("Your AWS session for account \"" + accountKey +
                                     "\" has expired.\nPlease sign in again to continue.");
    form->addRow(messageLabel);

    auto *usernameEdit = new QLineEdit(&dialog);
    usernameEdit->setText(initialUsername);
    auto *passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);

    form->addRow("Username:", usernameEdit);
    form->addRow("Password:", passwordEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Login");
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return result;

    const QString username = usernameEdit->text();
    const QString password = passwordEdit->text();
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(parent, "Login", "Username and password are both required.");
        return result;
    }

    const long appId = Configuration::instance().GetValue<long>("onelogin.app-id." + accountKey, 0L);
    if (appId == 0) {
        QMessageBox::warning(parent, "Login", "No OneLogin app-id configured for account \"" + accountKey + "\".");
        return result;
    }

    AwsSessionCredentials credentials;
    QString error;
    {
        BusyGuard busyGuard;
        credentials = LoginWithOneLogin(
            username, password, [parent] { return OtpDialog::Prompt(parent); }, appId, &error);
    }

    if (!credentials.isValid()) {
        QMessageBox::warning(parent, "Login failed", error.isEmpty() ? "Unknown error." : error);
        return result;
    }

    result.success = true;
    result.username = username;
    result.credentials = credentials;
    return result;
}
