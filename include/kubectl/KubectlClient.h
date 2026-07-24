#pragma once

// Qt includes
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

// Awsmock includes
#include <onelogin/OneLoginAuth.h>

// Circular indeterminate progress indicator: a rotating partial ring, redrawn on a timer.
class SpinnerWidget : public QWidget {
public:
    explicit SpinnerWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QTimer timer_;
    int angle_ = 0;
};

// Semi-transparent overlay with a busy indicator, shown over the main window
// whenever a kubectl call takes longer than a second.
class BusyOverlay : public QWidget {
public:
    explicit BusyOverlay(QWidget *parent);

    void showOverParent();
};

struct KubectlResult {
    bool success = false;
    QString output;
    QString error;
};

namespace KubectlClient {
    // Path to the kubeconfig file used for every kubectl invocation.
    const QString &Kubeconfig();

    // Points the busy overlay used by BusyGuard/runKubectlCommand at the given widget.
    // Must be called once (by MainWindow) before any kubectl call is made.
    void SetBusyOverlay(BusyOverlay *overlay);

    // Session credentials from the most recent successful OneLogin login for whichever
    // AWS account matches the currently selected kubectl context, if any. Injected into
    // kubectl's process environment so its exec-based auth plugin picks them up.
    void SetActiveAwsCredentials(const AwsSessionCredentials &credentials);
    const AwsSessionCredentials &ActiveAwsCredentials();

    int busyIndicatorDelayMs();

    KubectlResult runKubectlCommand(const QStringList &args, const QString &stdinData = QString());
    QString runKubectl(const QStringList &args);
    QJsonArray fetchItems(const QStringList &args);
} // namespace KubectlClient

// RAII guard that shows the busy overlay if the guarded scope (which may span
// several sequential kubectl calls) is still running after the configured delay.
class BusyGuard {
public:
    BusyGuard();
    ~BusyGuard();

private:
    QTimer timer_;
};
