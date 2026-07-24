#include <dialogs/OtpDialog.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

QString OtpDialog::Prompt(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("OneLogin Sign In");

    auto *form = new QFormLayout(&dialog);
    auto *otpEdit = new QLineEdit(&dialog);
    form->addRow("Authenticator Token", otpEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Verify");
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return {};
    return otpEdit->text();
}
