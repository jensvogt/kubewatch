#include <dialogs/EditResourceDialog.h>

#include <kubectl/KubectlClient.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

bool EditResourceDialog::Show(QWidget *parent, const QStringList &baseArgs, const QString &resource,
                               const QString &name, const QString &ns) {
    QStringList getArgs = baseArgs;
    getArgs << "get" << resource << name;
    if (!ns.isEmpty()) getArgs << "-n" << ns;
    getArgs << "-o" << "yaml";

    const auto [success, output, error] = KubectlClient::runKubectlCommand(getArgs);
    if (!success) {
        QMessageBox::warning(parent, "Edit failed", error);
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("Edit " + resource + "/" + name);
    dialog.resize(700, 700);
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(output);
    layout->addWidget(editor);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return false;

    QStringList applyArgs = baseArgs;
    applyArgs << "apply" << "-f" << "-";
    const KubectlResult applyResult = KubectlClient::runKubectlCommand(applyArgs, editor->toPlainText());
    if (!applyResult.success) {
        QMessageBox::warning(parent, "Apply failed", applyResult.error);
    }
    return true;
}
