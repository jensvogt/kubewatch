#include <dialogs/LogsDialog.h>

#include <kubectl/KubectlClient.h>
#include <utils/IconUtils.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

void LogsDialog::Show(QWidget *parent, const QStringList &baseArgs, const QString &resource, const QString &name,
                       const QString &ns) {
    QJsonArray pods;
    QString initialLogs;
    {
        BusyGuard busyGuard;
        if (resource == "pods") {
            QStringList getArgs = baseArgs;
            getArgs << "get" << "pods" << name << "-n" << ns << "-o" << "json";
            const KubectlResult podResult = KubectlClient::runKubectlCommand(getArgs);
            if (!podResult.success) {
                QMessageBox::warning(parent, "Logs failed", podResult.error);
                return;
            }
            pods.append(QJsonDocument::fromJson(podResult.output.toUtf8()).object());
        } else {
            QStringList podArgs = baseArgs;
            podArgs << "get" << "pods" << "-n" << ns << "-l" << "job-name=" + name << "-o" << "json";
            pods = KubectlClient::fetchItems(podArgs);
        }
        if (pods.isEmpty()) {
            QMessageBox::warning(parent, "Logs failed", "No pods found for " + resource + "/" + name);
            return;
        }

        const QJsonObject firstPod = pods[0].toObject();
        const QJsonArray firstContainers = firstPod["spec"].toObject()["containers"].toArray();
        if (!firstContainers.isEmpty()) {
            QStringList logArgs = baseArgs;
            logArgs << "logs" << firstPod["metadata"].toObject()["name"].toString() << "-n" << ns << "-c"
                    << firstContainers[0].toObject()["name"].toString() << "--tail=2000";
            const KubectlResult logResult = KubectlClient::runKubectlCommand(logArgs);
            initialLogs = logResult.success ? logResult.output : "Failed to fetch logs: " + logResult.error;
        }
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("Logs: " + ns + "/" + name);
    dialog.resize(1000, 700);

    auto *layout = new QVBoxLayout(&dialog);

    auto *topBar = new QHBoxLayout();
    topBar->addWidget(new QLabel("Pod:"));
    auto *podBox = new QComboBox(&dialog);
    for (const auto &podValue: pods) {
        podBox->addItem(podValue.toObject()["metadata"].toObject()["name"].toString());
    }
    topBar->addWidget(podBox);
    topBar->addWidget(new QLabel("Container:"));
    auto *containerBox = new QComboBox(&dialog);
    topBar->addWidget(containerBox);
    auto *previousButton = new QToolButton(&dialog);
    previousButton->setIcon(IconUtils::GetIcon("previous"));
    previousButton->setCheckable(true);
    previousButton->setToolTip("Show logs from the previous container instance (before the last restart)");
    topBar->addWidget(previousButton);
    topBar->addStretch();
    auto *autoScrollButton = new QToolButton(&dialog);
    autoScrollButton->setIcon(IconUtils::GetIcon("scroll"));
    autoScrollButton->setCheckable(true);
    autoScrollButton->setChecked(true);
    autoScrollButton->setToolTip("Auto-scroll to bottom");
    topBar->addWidget(autoScrollButton);
    auto *refreshButton = new QPushButton(&dialog);
    refreshButton->setIcon(IconUtils::GetIcon("refresh"));
    refreshButton->setToolTip("Refresh");
    topBar->addWidget(refreshButton);
    layout->addLayout(topBar);

    auto *logView = new QPlainTextEdit(&dialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont monoFont("Courier New");
    monoFont.setStyleHint(QFont::Monospace);
    logView->setFont(monoFont);
    layout->addWidget(logView);

    auto *scrollToStartShortcut = new QShortcut(QKeySequence("Ctrl+Home"), &dialog);
    QObject::connect(scrollToStartShortcut, &QShortcut::activated, logView, [logView] {
        logView->moveCursor(QTextCursor::Start);
        logView->ensureCursorVisible();
    });
    auto *scrollToEndShortcut = new QShortcut(QKeySequence("Ctrl+End"), &dialog);
    QObject::connect(scrollToEndShortcut, &QShortcut::activated, logView, [logView] {
        logView->moveCursor(QTextCursor::End);
        logView->ensureCursorVisible();
    });

    auto updateContainers = [&pods, podBox, containerBox] {
        containerBox->clear();
        const QJsonObject pod = pods[podBox->currentIndex()].toObject();
        for (const auto &containerValue: pod["spec"].toObject()["containers"].toArray()) {
            containerBox->addItem(containerValue.toObject()["name"].toString());
        }
    };
    updateContainers();

    auto setLogText = [logView, autoScrollButton](const QString &text) {
        logView->setPlainText(text);
        if (autoScrollButton->isChecked()) {
            logView->moveCursor(QTextCursor::End);
            logView->ensureCursorVisible();
        }
    };

    auto fetchLogs = [baseArgs, podBox, containerBox, previousButton, ns, setLogText] {
        if (containerBox->currentText().isEmpty()) return;
        BusyGuard busyGuard;
        QStringList logArgs = baseArgs;
        logArgs << "logs" << podBox->currentText() << "-n" << ns << "-c" << containerBox->currentText()
                << "--tail=2000";
        if (previousButton->isChecked()) logArgs << "--previous";
        const KubectlResult logResult = KubectlClient::runKubectlCommand(logArgs);
        setLogText(logResult.success ? logResult.output : "Failed to fetch logs: " + logResult.error);
    };

    QObject::connect(podBox, &QComboBox::currentIndexChanged, &dialog, [updateContainers, fetchLogs](int) {
        updateContainers();
        fetchLogs();
    });
    QObject::connect(containerBox, &QComboBox::currentIndexChanged, &dialog, [fetchLogs](int) { fetchLogs(); });
    QObject::connect(previousButton, &QToolButton::toggled, &dialog, [fetchLogs](bool) { fetchLogs(); });
    QObject::connect(refreshButton, &QPushButton::clicked, &dialog, [fetchLogs] { fetchLogs(); });

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(closeButtons);

    setLogText(initialLogs);

    dialog.exec();
}
