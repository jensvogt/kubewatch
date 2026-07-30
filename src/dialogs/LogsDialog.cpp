#include <dialogs/LogsDialog.h>

#include <kubectl/KubectlClient.h>
#include <kubectl/KubeNetService.h>
#include <utils/IconUtils.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
    // GET .../pods/<pod>/log?container=<container>&tailLines=2000[&previous=true] -- plain
    // text, not JSON, so this goes through fetchRaw rather than fetchItems/fetchObject.
    QString fetchPodLog(KubeNetService &svc, const QString &ns, const QString &podName, const QString &containerName,
                         bool previous, QString *error) {
        QString path = KubeNetService::ResourcePath("pods", ns) + "/" + podName + "/log?container=" + containerName + "&tailLines=2000";
        if (previous) path += "&previous=true";
        return QString::fromUtf8(svc.fetchRaw(path, error));
    }
} // namespace

void LogsDialog::Show(QWidget *parent, const QString &context, const QString &resource, const QString &name,
                       const QString &ns) {
    QJsonArray pods;
    QString initialLogs;
    {
        BusyGuard busyGuard;

        KubeNetService &svc = KubeNetService::forContext(context);
        if (!svc.IsValid()) {
            QMessageBox::warning(parent, "Logs failed", svc.LastError());
            return;
        }

        if (resource == "pods") {
            const QJsonObject podObj = svc.fetchObject(KubeNetService::ResourcePath("pods", ns) + "/" + name);
            if (podObj.isEmpty()) {
                QMessageBox::warning(parent, "Logs failed", svc.LastError());
                return;
            }
            pods.append(podObj);
        } else {
            pods = svc.fetchItems(KubeNetService::ResourcePath("pods", ns) + "?labelSelector=job-name=" + name);
        }
        if (pods.isEmpty()) {
            QMessageBox::warning(parent, "Logs failed", "No pods found for " + resource + "/" + name);
            return;
        }

        const QJsonObject firstPod = pods[0].toObject();
        const QJsonArray firstContainers = firstPod["spec"].toObject()["containers"].toArray();
        if (!firstContainers.isEmpty()) {
            QString error;
            initialLogs = fetchPodLog(svc, ns, firstPod["metadata"].toObject()["name"].toString(),
                                       firstContainers[0].toObject()["name"].toString(), false, &error);
            if (!error.isEmpty()) initialLogs = "Failed to fetch logs: " + error;
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

    auto fetchLogs = [context, podBox, containerBox, previousButton, ns, setLogText] {
        if (containerBox->currentText().isEmpty()) return;
        BusyGuard busyGuard;
        QString error;
        const QString logText = fetchPodLog(KubeNetService::forContext(context), ns, podBox->currentText(),
                                             containerBox->currentText(), previousButton->isChecked(), &error);
        setLogText(error.isEmpty() ? logText : "Failed to fetch logs: " + error);
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
