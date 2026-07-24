#include <kubectl/KubectlClient.h>

#include <utils/Configuration.h>
#include <utils/Logging.h>

#include <QApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>

SpinnerWidget::SpinnerWidget(QWidget *parent) : QWidget(parent) {
    setFixedSize(48, 48);
    connect(&timer_, &QTimer::timeout, this, [this] {
        angle_ = (angle_ + 8) % 360;
        update();
    });
    timer_.start(16);
}

void SpinnerWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    constexpr qreal penWidth = 5.0;
    const QRectF arcRect = rect().adjusted(penWidth / 2, penWidth / 2, -penWidth / 2, -penWidth / 2);

    QPen pen(QColor(240, 240, 240));
    pen.setWidthF(penWidth);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    constexpr int spanAngle = 100 * 16; // 100 degrees, in 1/16th-degree units
    painter.drawArc(arcRect, -angle_ * 16, spanAngle);
}

BusyOverlay::BusyOverlay(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(new SpinnerWidget(this));

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0, 0, 0, 140));
    setPalette(pal);
    hide();
}

void BusyOverlay::showOverParent() {
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    show();
    raise();
}

namespace {
    BusyOverlay *g_busyOverlay = nullptr;
    AwsSessionCredentials g_activeAwsCredentials;

    // Number of busy scopes/calls currently in flight. The overlay is only actually
    // hidden once this drops back to zero, so a sequence of kubectl calls nested
    // inside an outer BusyGuard doesn't flicker hide/show between each call.
    int g_busyDepth = 0;

    void exitBusyScope() {
        if (--g_busyDepth <= 0 && g_busyOverlay) {
            g_busyOverlay->hide();
        }
    }
} // namespace

namespace KubectlClient {
    const QString &Kubeconfig() {
        static const QString kubeconfig = QDir::homePath() + "/.kube/config";
        return kubeconfig;
    }

    void SetBusyOverlay(BusyOverlay *overlay) {
        g_busyOverlay = overlay;
    }

    void SetActiveAwsCredentials(const AwsSessionCredentials &credentials) {
        g_activeAwsCredentials = credentials;
    }

    const AwsSessionCredentials &ActiveAwsCredentials() {
        return g_activeAwsCredentials;
    }

    int busyIndicatorDelayMs() {
        return Configuration::instance().GetValue<int>("ui.busy-indicator-delay-ms", 500);
    }

    KubectlResult runKubectlCommand(const QStringList &args, const QString &stdinData) {
        logDebug << "kubectl" << args.join(' ');
        QProcess process;
        if (g_activeAwsCredentials.isValid()) {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("AWS_ACCESS_KEY_ID", g_activeAwsCredentials.accessKeyId);
            env.insert("AWS_SECRET_ACCESS_KEY", g_activeAwsCredentials.secretAccessKey);
            env.insert("AWS_SESSION_TOKEN", g_activeAwsCredentials.sessionToken);
            process.setProcessEnvironment(env);
        }
        process.start("kubectl", args);
        if (!process.waitForStarted(5000)) {
            return {false, {}, "Failed to start kubectl"};
        }
        if (!stdinData.isEmpty()) {
            process.write(stdinData.toUtf8());
        }
        process.closeWriteChannel();

        QElapsedTimer elapsed;
        elapsed.start();
        const int delayMs = busyIndicatorDelayMs();
        bool overlayShown = false;
        bool finished = false;
        ++g_busyDepth;
        while (elapsed.elapsed() < 30000) {
            if (process.waitForFinished(50)) {
                finished = true;
                break;
            }
            if (!overlayShown && elapsed.elapsed() > delayMs && g_busyOverlay) {
                g_busyOverlay->showOverParent();
                overlayShown = true;
            }
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        exitBusyScope();
        if (!finished) {
            return {false, {}, "kubectl timed out"};
        }

        KubectlResult result;
        result.output = QString::fromLocal8Bit(process.readAllStandardOutput());
        result.error = QString::fromLocal8Bit(process.readAllStandardError());
        result.success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        if (!result.success) {
            logWarning << "kubectl failed:" << result.error;
        }
        return result;
    }

    QString runKubectl(const QStringList &args) {
        const KubectlResult result = runKubectlCommand(args);
        return result.success ? result.output : QString();
    }

    QJsonArray fetchItems(const QStringList &args) {
        return QJsonDocument::fromJson(runKubectl(args).toUtf8()).object().value("items").toArray();
    }
} // namespace KubectlClient

BusyGuard::BusyGuard() {
    ++g_busyDepth;
    timer_.setSingleShot(true);
    QObject::connect(&timer_, &QTimer::timeout, [] {
        if (g_busyOverlay) g_busyOverlay->showOverParent();
    });
    timer_.start(KubectlClient::busyIndicatorDelayMs());
}

BusyGuard::~BusyGuard() {
    timer_.stop();
    exitBusyScope();
}
