#include <dialogs/LoginDialog.h>

#include <dialogs/OtpDialog.h>
#include <kubectl/KubectlClient.h>
#include <utils/Configuration.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

LoginDialog::Result LoginDialog::Show(QWidget *parent, const QStringList &contexts, const QString &currentContext,
                                       const QString &initialUsername) {
    Result result;

    QDialog dialog(parent);
    dialog.setWindowTitle("OneLogin Sign In");
    dialog.setMinimumWidth(450);

    auto *form = new QFormLayout(&dialog);

    auto *contextCombo = new QComboBox(&dialog);
    contextCombo->addItems(contexts);
    contextCombo->setCurrentText(currentContext);
    form->addRow("Context", contextCombo);

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

    const QString selectedContext = contextCombo->currentText();
    if (selectedContext.isEmpty()) {
        QMessageBox::warning(parent, "Login", "No context selected.");
        return result;
    }
    result.selectedContext = selectedContext;

    const QString username = usernameEdit->text();
    const QString password = passwordEdit->text();
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(parent, "Login", "Username and password are both required.");
        return result;
    }

    const QString accountKey = selectedContext.contains("prod", Qt::CaseInsensitive) ? "prod" : "int";
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
    result.accountKey = accountKey;
    result.credentials = credentials;
    return result;
}
