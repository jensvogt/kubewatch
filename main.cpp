#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <Version.h>
#include <components/PageableTable.h>
#include <onelogin/OneLoginAuth.h>
#include <utils/Configuration.h>
#include <utils/EventBus.h>
#include <utils/IconUtils.h>
#include <utils/Logging.h>
#include <utils/UpdateChecker.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace {
    const QString kKubeconfig = QDir::homePath() + "/.kube/config";

    // Circular indeterminate progress indicator: a rotating partial ring, redrawn on a timer.
    class SpinnerWidget : public QWidget {
    public:
        explicit SpinnerWidget(QWidget *parent = nullptr) : QWidget(parent) {
            setFixedSize(48, 48);
            connect(&timer_, &QTimer::timeout, this, [this] {
                angle_ = (angle_ + 8) % 360;
                update();
            });
            timer_.start(16);
        }

    protected:
        void paintEvent(QPaintEvent *) override {
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

    private:
        QTimer timer_;
        int angle_ = 0;
    };

    // Small donut gauge used by the Node detail dialog's Allocation section: a colored
    // ring filled to `percent` (green/orange/red by threshold), with the percentage and
    // a short label centered inside, and a caption (e.g. "Cores: 2.95") below the ring.
    class PercentageRing : public QWidget {
    public:
        PercentageRing(double percent, QString label, QString caption, QWidget *parent = nullptr)
            : QWidget(parent), percent_(percent), label_(std::move(label)), caption_(std::move(caption)) {
            setFixedSize(110, 132);
        }

    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);

            constexpr qreal penWidth = 8.0;
            const QRectF ringRect(penWidth / 2, penWidth / 2, 100 - penWidth, 100 - penWidth);

            const QColor color = percent_ < 60 ? QColor(76, 175, 80)
                                  : percent_ < 90 ? QColor(255, 152, 0)
                                                  : QColor(229, 57, 53);

            QPen bgPen(QColor(70, 70, 70));
            bgPen.setWidthF(penWidth);
            painter.setPen(bgPen);
            painter.drawEllipse(ringRect);

            QPen fgPen(color);
            fgPen.setWidthF(penWidth);
            fgPen.setCapStyle(Qt::RoundCap);
            painter.setPen(fgPen);
            const int spanAngle = -static_cast<int>(std::clamp(percent_, 0.0, 100.0) / 100.0 * 360 * 16);
            painter.drawArc(ringRect, 90 * 16, spanAngle);

            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(QRectF(0, 0, 100, 100), Qt::AlignCenter,
                              QString("%1%\n%2").arg(QString::number(percent_, 'f', 1), label_));
            painter.drawText(QRectF(0, 100, 100, 30), Qt::AlignHCenter | Qt::AlignTop, caption_);
        }

    private:
        double percent_;
        QString label_;
        QString caption_;
    };

    // Semi-transparent overlay with a busy indicator, shown over the main window
    // whenever a kubectl call takes longer than a second.
    class BusyOverlay : public QWidget {
    public:
        explicit BusyOverlay(QWidget *parent) : QWidget(parent) {
            auto *layout = new QVBoxLayout(this);
            layout->setAlignment(Qt::AlignCenter);
            layout->addWidget(new SpinnerWidget(this));

            setAutoFillBackground(true);
            QPalette pal = palette();
            pal.setColor(QPalette::Window, QColor(0, 0, 0, 140));
            setPalette(pal);
            hide();
        }

        void showOverParent() {
            if (parentWidget()) {
                setGeometry(parentWidget()->rect());
            }
            show();
            raise();
        }
    };

    BusyOverlay *g_busyOverlay = nullptr;

    // Session credentials from the most recent successful OneLogin login for whichever
    // AWS account matches the currently selected kubectl context, if any. Injected into
    // kubectl's process environment so its exec-based auth plugin picks them up.
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

    int busyIndicatorDelayMs() {
        return Configuration::instance().GetValue<int>("ui.busy-indicator-delay-ms", 500);
    }

    // RAII guard that shows the busy overlay if the guarded scope (which may span
    // several sequential kubectl calls) is still running after the configured delay.
    class BusyGuard {
    public:
        BusyGuard() {
            ++g_busyDepth;
            timer_.setSingleShot(true);
            QObject::connect(&timer_, &QTimer::timeout, [] {
                if (g_busyOverlay) g_busyOverlay->showOverParent();
            });
            timer_.start(busyIndicatorDelayMs());
        }

        ~BusyGuard() {
            timer_.stop();
            exitBusyScope();
        }

    private:
        QTimer timer_;
    };

    struct KubectlResult {
        bool success = false;
        QString output;
        QString error;
    };

    KubectlResult runKubectlCommand(const QStringList &args, const QString &stdinData = QString()) {
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

    QString computeAge(const QString &creationTimestamp) {
        const QDateTime created = QDateTime::fromString(creationTimestamp, Qt::ISODate);
        if (!created.isValid()) {
            return {};
        }
        const qint64 secs = created.secsTo(QDateTime::currentDateTimeUtc());
        if (secs < 60) return QString("%1s").arg(secs);
        if (secs < 3600) return QString("%1m").arg(secs / 60);
        if (secs < 86400) return QString("%1h").arg(secs / 3600);
        return QString("%1d").arg(secs / 86400);
    }

    QString formatCreated(const QString &creationTimestamp) {
        const QDateTime created = QDateTime::fromString(creationTimestamp, Qt::ISODate);
        if (!created.isValid()) {
            return {};
        }
        return created.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
    }

    // Parses a Kubernetes CPU quantity ("500m" or "2") into millicores.
    qint64 parseCpuMillis(const QString &value) {
        if (value.isEmpty()) return 0;
        if (value.endsWith('m')) {
            return value.chopped(1).toLongLong();
        }
        bool ok = false;
        const double cores = value.toDouble(&ok);
        return ok ? static_cast<qint64>(cores * 1000) : 0;
    }

    QString formatCpuMillis(qint64 millis) {
        if (millis % 1000 == 0) {
            return QString::number(millis / 1000);
        }
        return QString("%1m").arg(millis);
    }

    // Parses a Kubernetes memory quantity ("128Mi", "2Gi", "512k", plain bytes) into bytes.
    qint64 parseMemoryBytes(const QString &value) {
        if (value.isEmpty()) return 0;
        static const QList<std::pair<QString, qint64>> suffixes = {
            {"Ki", 1024LL}, {"Mi", 1024LL * 1024}, {"Gi", 1024LL * 1024 * 1024}, {"Ti", 1024LL * 1024 * 1024 * 1024},
            {"K", 1000LL}, {"M", 1000LL * 1000}, {"G", 1000LL * 1000 * 1000}, {"T", 1000LL * 1000 * 1000 * 1000},
        };
        for (const auto &[suffix, factor]: suffixes) {
            if (value.endsWith(suffix)) {
                return static_cast<qint64>(value.chopped(suffix.size()).toDouble() * factor);
            }
        }
        bool ok = false;
        const qint64 bytes = value.toLongLong(&ok);
        return ok ? bytes : 0;
    }

    QString formatMemoryGiB(qint64 bytes) {
        return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " Gi";
    }

    // Succeeded/Failed pods linger in the API (e.g. completed Job/CronJob pods) without
    // holding any node resources. kubectl describe node's "Non-terminated Pods" summary
    // (and the Kubernetes Dashboard's node Allocation section) exclude them for exactly
    // this reason -- otherwise counts/CPU/memory sums come out inflated.
    bool isTerminatedPodPhase(const QString &phase) {
        return phase == "Succeeded" || phase == "Failed";
    }

    QString joinKeyValues(const QJsonObject &obj) {
        QStringList pairs;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            pairs << it.key() + ": " + it.value().toString();
        }
        return pairs.isEmpty() ? "-" : pairs.join("\n");
    }

    QTableWidget *makeDetailTable(const QStringList &headers) {
        auto *table = new QTableWidget();
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        return table;
    }

    // Builds one titled column of the Allocation section (e.g. "CPU") out of one or more
    // side-by-side PercentageRing gauges (e.g. Requests/Limits).
    QWidget *makeAllocationColumn(const QString &title, const QList<PercentageRing *> &rings) {
        auto *column = new QWidget();
        auto *columnLayout = new QVBoxLayout(column);
        auto *titleLabel = new QLabel(title);
        titleLabel->setAlignment(Qt::AlignHCenter);
        columnLayout->addWidget(titleLabel);
        auto *ringsRow = new QHBoxLayout();
        for (auto *ring: rings) {
            ringsRow->addWidget(ring);
        }
        columnLayout->addLayout(ringsRow);
        return column;
    }

    QJsonObject findByName(const QJsonArray &array, const QString &name) {
        for (const auto &value: array) {
            if (const QJsonObject obj = value.toObject(); obj["name"].toString() == name) return obj;
        }
        return {};
    }

    std::pair<QString, QString> volumeSourceInfo(const QJsonObject &volume) {
        if (volume.contains("emptyDir")) return {"EmptyDir", "-"};
        if (volume.contains("secret")) return {"Secret", volume["secret"].toObject()["secretName"].toString()};
        if (volume.contains("configMap")) return {"ConfigMap", volume["configMap"].toObject()["name"].toString()};
        if (volume.contains("persistentVolumeClaim"))
            return {"PersistentVolumeClaim", volume["persistentVolumeClaim"].toObject()["claimName"].toString()};
        if (volume.contains("projected")) return {"Projected", "-"};
        if (volume.contains("hostPath")) return {"HostPath", volume["hostPath"].toObject()["path"].toString()};
        return {"-", "-"};
    }

    QString formatProbeEndpoint(const QJsonObject &probe) {
        if (probe.contains("httpGet")) {
            const QJsonObject httpGet = probe["httpGet"].toObject();
            const QString host = httpGet["host"].toString().isEmpty() ? "[Pod IP]" : httpGet["host"].toString();
            return QString("HTTP GET %1:%2%3").arg(host, httpGet["port"].toVariant().toString(), httpGet["path"].toString());
        }
        if (probe.contains("tcpSocket")) {
            return "TCP " + probe["tcpSocket"].toObject()["port"].toVariant().toString();
        }
        if (probe.contains("exec")) {
            QStringList command;
            for (const auto &value: probe["exec"].toObject()["command"].toArray()) {
                command << value.toString();
            }
            return "Exec: " + command.join(" ");
        }
        return "-";
    }

    QString formatResourceList(const QJsonObject &resourceMap) {
        QStringList parts;
        for (auto it = resourceMap.begin(); it != resourceMap.end(); ++it) {
            parts << it.key() + ": " + it.value().toString();
        }
        return parts.isEmpty() ? "-" : parts.join(", ");
    }

    QGroupBox *buildContainerBox(const QJsonObject &containerSpec, const QJsonObject &containerStatus,const QJsonArray &volumes) {
        auto *box = new QGroupBox("Container: " + containerSpec["name"].toString());
        auto *layout = new QVBoxLayout(box);

        layout->addWidget(new QLabel("Image: " + containerSpec["image"].toString()));

        const QJsonObject state = containerStatus["state"].toObject();
        const QString startedAt =
                state.contains("running") ? formatCreated(state["running"].toObject()["startedAt"].toString()) : "-";

        auto *statusForm = new QFormLayout();
        statusForm->addRow("Ready", new QLabel(containerStatus["ready"].toBool() ? "true" : "false"));
        statusForm->addRow("Started", new QLabel(containerStatus["started"].toBool() ? "true" : "false"));
        statusForm->addRow("Restart Count", new QLabel(QString::number(containerStatus["restartCount"].toInt())));
        statusForm->addRow("Started At", new QLabel(startedAt));
        layout->addLayout(statusForm);

        // The single most useful signal for "pod restarted but the app's own logs show
        // nothing" -- covers kills the process never got to log (OOMKilled, a failed
        // liveness probe, node-triggered SIGKILL/SIGTERM).
        if (const QJsonObject lastTerminated = containerStatus["lastState"].toObject()["terminated"].toObject();
            !lastTerminated.isEmpty()) {
            auto *lastStateBox = new QGroupBox("Last Termination");
            auto *lastStateForm = new QFormLayout(lastStateBox);
            lastStateForm->addRow("Reason", new QLabel(lastTerminated["reason"].toString()));
            lastStateForm->addRow("Exit Code", new QLabel(QString::number(lastTerminated["exitCode"].toInt())));
            if (const int signal = lastTerminated["signal"].toInt(); signal != 0) {
                lastStateForm->addRow("Signal", new QLabel(QString::number(signal)));
            }
            lastStateForm->addRow("Started At", new QLabel(formatCreated(lastTerminated["startedAt"].toString())));
            lastStateForm->addRow("Finished At", new QLabel(formatCreated(lastTerminated["finishedAt"].toString())));
            if (const QString message = lastTerminated["message"].toString(); !message.isEmpty()) {
                auto *messageLabel = new QLabel(message);
                messageLabel->setWordWrap(true);
                lastStateForm->addRow("Message", messageLabel);
            }
            layout->addWidget(lastStateBox);
        }

        QStringList envLines;
        for (const auto &envValue: containerSpec["env"].toArray()) {
            const QJsonObject env = envValue.toObject();
            const QString key = env["name"].toString();
            if (env.contains("value")) {
                envLines << key + "=" + env["value"].toString();
            } else if (env.contains("valueFrom")) {
                const QJsonObject valueFrom = env["valueFrom"].toObject();
                if (valueFrom.contains("secretKeyRef")) {
                    envLines << key + "=<secret:" + valueFrom["secretKeyRef"].toObject()["name"].toString() + ">";
                } else if (valueFrom.contains("configMapKeyRef")) {
                    envLines << key + "=<configmap:" + valueFrom["configMapKeyRef"].toObject()["name"].toString() + ">";
                } else if (valueFrom.contains("fieldRef")) {
                    envLines << key + "=<field:" + valueFrom["fieldRef"].toObject()["fieldPath"].toString() + ">";
                } else {
                    envLines << key + "=<from>";
                }
            }
        }
        if (!envLines.isEmpty()) {
            auto *envBox = new QGroupBox("Environment Variables");
            auto *envLayout = new QVBoxLayout(envBox);
            auto *envLabel = new QLabel(envLines.join("\n"));
            envLabel->setWordWrap(true);
            envLayout->addWidget(envLabel);
            layout->addWidget(envBox);
        }

        if (const QJsonArray mounts = containerSpec["volumeMounts"].toArray(); !mounts.isEmpty()) {
            auto *mountsTable =
                    makeDetailTable({"Name", "Read Only", "Mount Path", "Sub Path", "Source Type", "Source Name"});
            mountsTable->setRowCount(mounts.size());
            for (int i = 0; i < mounts.size(); ++i) {
                const QJsonObject mount = mounts[i].toObject();
                const auto [sourceType, sourceName] = volumeSourceInfo(findByName(volumes, mount["name"].toString()));
                mountsTable->setItem(i, 0, new QTableWidgetItem(mount["name"].toString()));
                mountsTable->setItem(i, 1, new QTableWidgetItem(mount["readOnly"].toBool() ? "true" : "false"));
                mountsTable->setItem(i, 2, new QTableWidgetItem(mount["mountPath"].toString()));
                const QString subPath = mount["subPath"].toString();
                mountsTable->setItem(i, 3, new QTableWidgetItem(subPath.isEmpty() ? "-" : subPath));
                mountsTable->setItem(i, 4, new QTableWidgetItem(sourceType));
                mountsTable->setItem(i, 5, new QTableWidgetItem(sourceName.isEmpty() ? "-" : sourceName));
            }
            auto *mountsBox = new QGroupBox("Mounts");
            auto *mountsLayout = new QVBoxLayout(mountsBox);
            mountsLayout->addWidget(mountsTable);
            layout->addWidget(mountsBox);
        }

        auto addProbe = [&](const QString &label, const QString &key) {
            if (!containerSpec.contains(key)) return;
            const QJsonObject probe = containerSpec[key].toObject();
            auto *probeBox = new QGroupBox(label);
            auto *probeForm = new QFormLayout(probeBox);
            probeForm->addRow("Initial Delay (s)", new QLabel(QString::number(probe["initialDelaySeconds"].toInt())));
            probeForm->addRow("Timeout (s)", new QLabel(QString::number(probe["timeoutSeconds"].toInt())));
            probeForm->addRow("Period (s)", new QLabel(QString::number(probe["periodSeconds"].toInt())));
            probeForm->addRow("Success Threshold", new QLabel(QString::number(probe["successThreshold"].toInt())));
            probeForm->addRow("Failure Threshold", new QLabel(QString::number(probe["failureThreshold"].toInt())));
            probeForm->addRow("Endpoint", new QLabel(formatProbeEndpoint(probe)));
            layout->addWidget(probeBox);
        };
        addProbe("Liveness Probe", "livenessProbe");
        addProbe("Readiness Probe", "readinessProbe");
        addProbe("Startup Probe", "startupProbe");

        const QJsonObject resources = containerSpec["resources"].toObject();
        auto *resourcesForm = new QFormLayout();
        resourcesForm->addRow("Limits", new QLabel(formatResourceList(resources["limits"].toObject())));
        resourcesForm->addRow("Requests", new QLabel(formatResourceList(resources["requests"].toObject())));
        layout->addLayout(resourcesForm);

        return box;
    }

    // Populates the table with the slice of items belonging to the table's current page.
    // Applies the table's own name-prefix filter to the full item set first, so the
    // pagination totals/labels reflect the filtered count rather than the unfiltered one.
    template<class RowFn>
    void populatePage(PageableTable *table, const QJsonArray &items, RowFn rowFn) {
        const QString prefix = table->GetPrefix();
        QJsonArray filtered;
        if (prefix.isEmpty()) {
            filtered = items;
        } else {
            for (const auto &item: items) {
                if (item.toObject()["metadata"].toObject()["name"].toString().startsWith(prefix)) {
                    filtered.append(item);
                }
            }
        }

        table->Clear();
        table->SetTotalSize(filtered.size());

        const long start = table->GetPageIndex() * table->GetPageSize();
        long end = start + table->GetPageSize();
        if (end > filtered.size()) end = filtered.size();

        for (long i = start; i < end; ++i) {
            rowFn(static_cast<int>(i - start), filtered[static_cast<int>(i)].toObject());
        }
    }

    enum class PageKind { Placeholder, Generic, Pods, Jobs, Services, Ingresses, Nodes, Namespaces, Settings };

    struct ResourceSpec {
        PageKind kind = PageKind::Placeholder;
        QString kubectlName;
        int namespaceColumn = -1; // -1 for cluster-scoped resources
    };

    PageableTable *makePageableTable(const QStringList &headers) {
        auto *table = new PageableTable();
        table->SetHeaderNames(headers);

        QList<QHeaderView::ResizeMode> resizeModes;
        resizeModes << QHeaderView::Stretch;
        for (int col = 1; col < headers.size(); ++col) {
            resizeModes << QHeaderView::ResizeToContents;
        }
        table->SetResizeModes(resizeModes);
        return table;
    }

    // Ensures a small local config file exists so Configuration/IconUtils resolve the dark icon set.
    QString ensureConfigFile() {
        const QString dir = QDir::homePath() + "/.kubewatch";
        if (!QDir().mkpath(dir)) {
            QMessageBox::warning(nullptr, "Create config directory","Could not create directory: " + dir);
            return {};
        }
        const QString path = dir + "/kubewatch.json";
        if (!QFile::exists(path)) {
            const QString installedDefault = QCoreApplication::applicationDirPath() + "/kubewatch.json";
            if (QFile::exists(installedDefault)) {
                QFile::copy(installedDefault, path);
            } else if (QFile file(path); file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(R"({"ui": {"style-type": "dark", "page-size": 25, "busy-indicator-delay-ms": 500}})");
                file.close();
            }
        }
        return path;
    }
} // namespace

namespace {
    class MainWindow : public QMainWindow {
    public:
        MainWindow() {
            setWindowTitle("KubeWatch");
            resize(1800, 1200);

            g_busyOverlay = new BusyOverlay(this);

            statusLabel_ = new QLabel("Last refresh: never");
            statusBar()->addPermanentWidget(statusLabel_);

            auto *toolbar = addToolBar("Main");
            toolbar->setMovable(false);
            toolbar->addWidget(new QLabel(" Context: "));
            contextBox_ = new QComboBox();
            toolbar->addWidget(contextBox_);
            toolbar->addWidget(new QLabel(" Namespace: "));
            namespaceBox_ = new QComboBox();
            toolbar->addWidget(namespaceBox_);

            auto *spacer = new QWidget();
            spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            toolbar->addWidget(spacer);

            auto *refreshAction = new QAction(QIcon(":/icons/dark/refresh.svg"), QString(), this);
            refreshAction->setToolTip("Refresh");
            connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshCurrentPage);
            toolbar->addAction(refreshAction);

            auto *updateAction = new QAction(IconUtils::GetIcon("update"), QString(), this);
            updateAction->setToolTip("Check for Update");
            connect(updateAction, &QAction::triggered, this, [this] { updateChecker_->checkForUpdates(); });
            toolbar->addAction(updateAction);

            auto *central = new QWidget(this);
            auto *mainLayout = new QVBoxLayout(central);

            auto *splitter = new QSplitter(Qt::Horizontal);

            tree_ = new QTreeWidget();
            tree_->setHeaderHidden(true);
            splitter->addWidget(tree_);

            pages_ = new QStackedWidget();
            splitter->addWidget(pages_);
            splitter->setStretchFactor(1, 1);

            buildTree();

            logWidget_ = new QListWidget();
            logWidget_->setUniformItemSizes(true);
            logWidget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
            logWidget_->setDragEnabled(true);
            // QFont logFont("Courier New");
            // logFont.setStyleHint(QFont::Monospace);
            // logWidget_->setFont(logFont);

            autoScrollLogsButton_ = new QToolButton();
            autoScrollLogsButton_->setIcon(IconUtils::GetIcon("scroll"));
            autoScrollLogsButton_->setCheckable(true);
            autoScrollLogsButton_->setChecked(true);
            autoScrollLogsButton_->setToolTip("Auto-scroll to bottom");

            connect(&LogSignaler::instance(), &LogSignaler::newLog, this, [this](const QString &msg) {
                logWidget_->addItem(msg);
                if (logWidget_->count() > 5000) {
                    delete logWidget_->takeItem(0);
                }
                if (autoScrollLogsButton_->isChecked()) {
                    logWidget_->scrollToBottom();
                }
            });

            auto *copyLogShortcut = new QShortcut(QKeySequence::Copy, logWidget_);
            connect(copyLogShortcut, &QShortcut::activated, this, [this] {
                QStringList lines;
                for (const QListWidgetItem *item : logWidget_->selectedItems()) {
                    lines << item->text();
                }
                if (!lines.isEmpty()) {
                    QApplication::clipboard()->setText(lines.join('\n'));
                }
            });

            auto *logContainer = new QWidget();
            auto *logContainerLayout = new QVBoxLayout(logContainer);
            logContainerLayout->setContentsMargins(0, 0, 0, 0);
            auto *logToolbar = new QHBoxLayout();
            logToolbar->addWidget(new QLabel("Log"));
            logToolbar->addStretch();
            logToolbar->addWidget(autoScrollLogsButton_);
            logContainerLayout->addLayout(logToolbar);
            logContainerLayout->addWidget(logWidget_);

            auto *verticalSplitter = new QSplitter(Qt::Vertical);
            verticalSplitter->addWidget(splitter);
            verticalSplitter->addWidget(logContainer);
            verticalSplitter->setStretchFactor(0, 1);
            verticalSplitter->setSizes({700, 200});

            mainLayout->addWidget(verticalSplitter);
            setCentralWidget(central);

            connect(tree_, &QTreeWidget::currentItemChanged, this, &MainWindow::onTreeSelectionChanged);
            connect(contextBox_, &QComboBox::currentIndexChanged, this, &MainWindow::onContextChanged);
            connect(namespaceBox_, &QComboBox::currentIndexChanged, this, &MainWindow::onNamespaceChanged);

            {
                const QSignalBlocker blocker(contextBox_);
                loadContexts();
                if (const auto savedContext = Configuration::instance().GetValue<QString>("ui.last-context", QString()); !savedContext.isEmpty()) {
                    if (const int idx = contextBox_->findText(savedContext); idx >= 0) contextBox_->setCurrentIndex(idx);
                }
            }

            {
                const QSignalBlocker blocker(namespaceBox_);
                loadNamespaces();
                const auto savedNamespace =
                    Configuration::instance().GetValue<QString>("ui.last-namespace", QString());
                if (!savedNamespace.isEmpty()) {
                    if (const int idx = namespaceBox_->findText(savedNamespace); idx >= 0) namespaceBox_->setCurrentIndex(idx);
                }
            }

            const auto savedNavItem = Configuration::instance().GetValue<QString>("ui.last-nav-item", QString());
            QTreeWidgetItem* restoredItem = savedNavItem.isEmpty() ? nullptr : findLeafByLabel(savedNavItem);
            tree_->setCurrentItem(restoredItem ? restoredItem : nodesItem_);

            // Show the login prompt once the event loop starts, so it appears on top of
            // the already-visible main window rather than blocking its own construction.
            QTimer::singleShot(0, this, &MainWindow::showLoginDialog);

            startUpdateChecker();
        }

    private:
        // Checks version.txt on GitHub Pages against APP_VERSION: once silently shortly
        // after startup (and periodically thereafter), and on demand via the toolbar
        // action (which also confirms when already up to date).
        void startUpdateChecker() {
            updateChecker_ = new UpdateChecker(this);
            connect(updateChecker_, &UpdateChecker::UpdateAvailable, this, [this](const QString &ver) {
                if (ver.isEmpty()) {
                    QMessageBox::information(this, "Check for Update", "You already have the latest version.");
                    return;
                }
                QMessageBox box(QMessageBox::Information, "Update Available",
                                 "kubewatch " + ver + " is available. You are running " + QString(APP_VERSION) + ".",
                                 QMessageBox::NoButton, this);
                box.addButton(QMessageBox::Close);
                QPushButton *downloadButton = box.addButton("Download", QMessageBox::ActionRole);
                box.exec();
                if (box.clickedButton() == downloadButton) {
                    QDesktopServices::openUrl(QUrl("https://jensvogt.github.io/kubewatch/"));
                }
            });
            updateChecker_->checkForUpdatesNoNotification();

            auto *updateTimer = new QTimer(this);
            const int intervalSeconds = Configuration::instance().GetValue<int>("general.update-check-period", 24 * 3600);
            updateTimer->setInterval(intervalSeconds * 1000);
            connect(updateTimer, &QTimer::timeout, this, [this] { updateChecker_->checkForUpdatesNoNotification(); });
            updateTimer->start();
        }

        [[nodiscard]]
        QStringList baseArgs() const { return {"--kubeconfig", kKubeconfig, "--context", contextBox_->currentText()}; }

        [[nodiscard]]
        QStringList resourceArgs(const QString &resource) const {
            QStringList args = baseArgs();
            args << "get" << resource;
            const QString ns = namespaceBox_->currentText();
            if (ns.isEmpty() || ns == "All namespaces") {
                args << "--all-namespaces";
            } else {
                args << "-n" << ns;
            }
            args << "-o" << "json";
            return args;
        }

        QTreeWidgetItem *addLeaf(QTreeWidgetItem *parent, const QString &label, const PageKind kind, const QString &kubectlName = QString()) {
            QStringList headers;
            bool namespaced = false;
            switch (kind) {
                case PageKind::Generic:
                    headers = {"Name", "Age"};
                    namespaced = true;
                    break;
                case PageKind::Pods:
                    headers = {"Name", "Ready", "Status", "Restarts", "Age"};
                    namespaced = true;
                    break;
                case PageKind::Jobs:
                    headers = {"Name", "Status", "Pods", "Created", "Age"};
                    namespaced = true;
                    break;
                case PageKind::Services:
                    headers = {"Name", "Type", "Cluster IP", "Created", "Age"};
                    namespaced = true;
                    break;
                case PageKind::Ingresses:
                    headers = {"Name", "Age"};
                    namespaced = true;
                    break;
                case PageKind::Nodes:
                    headers = {"Name", "Status", "Version", "Internal IP", "CPU Requests", "CPU Limits", "CPU Capacity", "Pods", "Created"};
                    break;
                case PageKind::Namespaces:
                    headers = {"Name", "Status", "Age"};
                    break;
                case PageKind::Settings:
                case PageKind::Placeholder:
                    break;
            }

            QWidget *page = nullptr;
            int namespaceColumn = -1;
            if (kind == PageKind::Settings) {
                page = new QLabel("Kubeconfig: " + kKubeconfig);
            } else if (kind == PageKind::Placeholder) {
                page = new QLabel("Select a resource from the tree on the left.");
            } else {
                if (namespaced) {
                    namespaceColumn = headers.size();
                    headers << "Namespace";
                }
                auto *table = makePageableTable(headers);
                if (namespaceColumn >= 0) {
                    table->SetHiddenColumns({namespaceColumn});
                }
                page = table;
            }

            const int pageIndex = pages_->addWidget(page);
            specs_.push_back({kind, kubectlName, namespaceColumn});

            if (auto *pageable = qobject_cast<PageableTable *>(page)) {
                pageable->setServiceApis({kubectlName});
                connect(pageable, &PageableTable::ReloadTable, this, &MainWindow::refreshCurrentPage);
                connect(pageable, &PageableTable::ContextMenuRequested, this,
                        [this, pageIndex](const QPoint &pos) { showTableContextMenu(pageIndex, pos); });
                if (kind == PageKind::Jobs) {
                    connect(pageable, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showJobDetailsForRow(pageIndex, index); });
                } else if (kind == PageKind::Pods) {
                    connect(pageable, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showPodDetailsForRow(pageIndex, index); });
                } else if (kind == PageKind::Services) {
                    connect(pageable, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showServiceDetailsForRow(pageIndex, index); });
                } else if (kind == PageKind::Ingresses) {
                    connect(pageable, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showIngressDetailsForRow(pageIndex, index); });
                } else if (kind == PageKind::Nodes) {
                    connect(pageable, &PageableTable::DoubleClicked, this,
                            [this, pageIndex](const QModelIndex &index) { showNodeDetailsForRow(pageIndex, index); });
                }
            }

            auto *item = parent ? new QTreeWidgetItem(parent, {label}) : new QTreeWidgetItem(tree_, {label});
            item->setData(0, Qt::UserRole, pageIndex);
            return item;
        }

        void buildTree() {
            pages_->addWidget(new QLabel("Select a resource from the tree on the left."));
            specs_.push_back({PageKind::Placeholder, QString()});

            auto *workloads = new QTreeWidgetItem(tree_, {"Workloads"});
            addLeaf(workloads, "Pods", PageKind::Pods, "pods");
            addLeaf(workloads, "Deployments", PageKind::Generic, "deployments");
            addLeaf(workloads, "StatefulSets", PageKind::Generic, "statefulsets");
            addLeaf(workloads, "DaemonSets", PageKind::Generic, "daemonsets");
            addLeaf(workloads, "Jobs", PageKind::Jobs, "jobs");

            auto *service = new QTreeWidgetItem(tree_, {"Service"});
            addLeaf(service, "Services", PageKind::Services, "services");
            addLeaf(service, "Ingresses", PageKind::Ingresses, "ingresses");

            auto *configStorage = new QTreeWidgetItem(tree_, {"Config and Storage"});
            addLeaf(configStorage, "ConfigMaps", PageKind::Generic, "configmaps");
            addLeaf(configStorage, "Secrets", PageKind::Generic, "secrets");
            addLeaf(configStorage, "PersistentVolumeClaims", PageKind::Generic, "persistentvolumeclaims");

            auto *cluster = new QTreeWidgetItem(tree_, {"Cluster"});
            nodesItem_ = addLeaf(cluster, "Nodes", PageKind::Nodes, "nodes");
            addLeaf(cluster, "Namespaces", PageKind::Namespaces, "namespaces");

            addLeaf(nullptr, "Settings", PageKind::Settings);

            tree_->expandAll();
        }

        void loadContexts() const {
            const QString result = runKubectl({"--kubeconfig", kKubeconfig, "config", "get-contexts", "-o", "name"});
            contextBox_->addItems(result.split('\n', Qt::SkipEmptyParts));

            const QString current = runKubectl({"--kubeconfig", kKubeconfig, "config", "current-context"}).trimmed();
            if (const int index = contextBox_->findText(current); index >= 0) {
                contextBox_->setCurrentIndex(index);
            }
        }

        void loadNamespaces() const {
            // Clearing/repopulating the combo changes its current index as a side effect,
            // which would otherwise fire onNamespaceChanged (and a spurious extra page
            // refresh) for each such change -- callers already trigger their own single
            // deliberate refresh once this returns.
            const QSignalBlocker blocker(namespaceBox_);
            namespaceBox_->clear();
            namespaceBox_->addItem("All namespaces");
            QStringList args = baseArgs();
            args << "get" << "namespaces" << "-o" << "json";
            for (const auto &item: fetchItems(args)) {
                namespaceBox_->addItem(item.toObject()["metadata"].toObject()["name"].toString());
            }
        }

        void onContextChanged() {
            Configuration::instance().SetValue("ui.last-context", contextBox_->currentText());
            // showLoginDialog() already refreshes credentials/namespaces/the current page
            // on success; only do it here as a fallback if login was cancelled or failed,
            // so switching context doesn't leave stale data on screen either way.
            if (!showLoginDialog()) {
                updateActiveAwsCredentials();
                loadNamespaces();
                refreshCurrentPage();
            }
        }

        // Derives which OneLogin/AWS account ("int" or "prod") the currently selected
        // kubectl context belongs to.
        [[nodiscard]] QString currentAwsAccountKey() const {
            return contextBox_->currentText().contains("prod", Qt::CaseInsensitive) ? "prod" : "int";
        }

        // Points g_activeAwsCredentials at whatever cached session (if any) applies to
        // the currently selected context, so runKubectlCommand picks it up.
        void updateActiveAwsCredentials() const {
            const auto it = awsCredentialsByAccount_.find(currentAwsAccountKey());
            g_activeAwsCredentials = it != awsCredentialsByAccount_.end() ? it.value() : AwsSessionCredentials{};
        }

        void onNamespaceChanged() const {
            Configuration::instance().SetValue("ui.last-namespace", namespaceBox_->currentText());
            refreshCurrentPage();
        }

        [[nodiscard]] QTreeWidgetItem* findLeafByLabel(const QString& label) const {
            QTreeWidgetItemIterator it(tree_);
            while (*it) {
                if ((*it)->text(0) == label && (*it)->data(0, Qt::UserRole).isValid()) {
                    return *it;
                }
                ++it;
            }
            return nullptr;
        }

        void onTreeSelectionChanged(QTreeWidgetItem *current, QTreeWidgetItem *) const {
            if (!current) return;
            const QVariant data = current->data(0, Qt::UserRole);
            if (!data.isValid()) {
                pages_->setCurrentIndex(0);
                return;
            }
            Configuration::instance().SetValue("ui.last-nav-item", current->text(0));
            const int pageIndex = data.toInt();
            pages_->setCurrentIndex(pageIndex);
            loadPage(pageIndex);
        }

        void refreshCurrentPage() const {
            const QTreeWidgetItem *current = tree_->currentItem();
            if (!current) return;
            const QVariant data = current->data(0, Qt::UserRole);
            if (!data.isValid()) return;
            loadPage(data.toInt());
        }

        void loadPage(const int pageIndex) const {
            const auto &[kind, kubectlName, namespaceColumn] = specs_[pageIndex];
            auto *table = qobject_cast<PageableTable *>(pages_->widget(pageIndex));
            switch (kind) {
                case PageKind::Generic:
                    fetchGeneric(table, kubectlName, namespaceColumn);
                    break;
                case PageKind::Pods:
                    fetchPods(table, namespaceColumn);
                    break;
                case PageKind::Jobs:
                    fetchJobs(table, namespaceColumn);
                    break;
                case PageKind::Services:
                    fetchServices(table, namespaceColumn);
                    break;
                case PageKind::Ingresses:
                    fetchGeneric(table, kubectlName, namespaceColumn);
                    break;
                case PageKind::Nodes:
                    fetchNodes(table);
                    break;
                case PageKind::Namespaces:
                    fetchNamespaces(table);
                    break;
                case PageKind::Settings:
                case PageKind::Placeholder:
                    return;
            }
            statusLabel_->setText("Last refresh: " + QDateTime::currentDateTime().toString("HH:mm:ss"));
        }

        void fetchGeneric(PageableTable *table, const QString &resource, const int namespaceColumn) const {
            if (!table) return;
            QElapsedTimer timer;
            timer.start();
            const QJsonArray items = fetchItems(resourceArgs(resource));

            populatePage(table, items, [&](int row, const QJsonObject &obj) {
                const QJsonObject metadata = obj["metadata"].toObject();
                table->SetColumn(row, 0, metadata["name"].toString());
                table->SetColumn(row, 1, computeAge(metadata["creationTimestamp"].toString()));
                table->SetHiddenColumn(row, namespaceColumn, metadata["namespace"].toString());
            });
            EventBus::instance().TimerSignal(resource, timer.elapsed());
        }

        void fetchPods(PageableTable *table, const int namespaceColumn) const {
            if (!table) return;
            QElapsedTimer timer;
            timer.start();
            const QJsonArray items = fetchItems(resourceArgs("pods"));

            populatePage(table, items, [&](const int row, const QJsonObject &pod) {
                const QJsonObject metadata = pod["metadata"].toObject();
                const QJsonObject status = pod["status"].toObject();

                QString podStatus = status["phase"].toString();
                int restarts = 0;
                int readyContainers = 0;
                const QJsonArray containerStatuses = status["containerStatuses"].toArray();
                for (const auto &containerStatusValue: containerStatuses) {
                    const QJsonObject containerStatus = containerStatusValue.toObject();
                    restarts += containerStatus["restartCount"].toInt();
                    if (containerStatus["ready"].toBool()) {
                        ++readyContainers;
                    }

                    if (const QJsonObject waiting = containerStatus["state"].toObject()["waiting"].toObject(); !waiting.isEmpty()) {
                        podStatus = waiting["reason"].toString();
                    }
                }
                if (metadata.contains("deletionTimestamp")) {
                    podStatus = "Terminating";
                }
                const QString ready = QString("%1/%2").arg(readyContainers).arg(containerStatuses.size());

                table->SetColumn(row, 0, metadata["name"].toString());
                table->SetColumn(row, 1, ready);
                table->SetColumn(row, 2, podStatus);
                table->SetColumn(row, 3, static_cast<long>(restarts));
                table->SetColumn(row, 4, computeAge(metadata["creationTimestamp"].toString()));
                table->SetHiddenColumn(row, namespaceColumn, metadata["namespace"].toString());
            });
            EventBus::instance().TimerSignal("pods", timer.elapsed());
        }

        void fetchJobs(PageableTable *table, int namespaceColumn) const {
            if (!table) return;
            QElapsedTimer timer;
            timer.start();
            const QJsonArray items = fetchItems(resourceArgs("jobs"));

            populatePage(table, items, [&](int row, const QJsonObject &job) {
                const QJsonObject metadata = job["metadata"].toObject();
                const QJsonObject status = job["status"].toObject();
                const QJsonObject spec = job["spec"].toObject();

                QString jobStatus = "Running";
                for (const auto &conditionValue: status["conditions"].toArray()) {
                    if (const QJsonObject condition = conditionValue.toObject(); condition["status"].toString() == "True") {
                        jobStatus = condition["type"].toString();
                    }
                }

                const int running = status["active"].toInt();
                const QJsonValue completions = spec["completions"];
                const QJsonValue parallelism = spec["parallelism"];
                const QString requested = completions.isDouble()
                                              ? QString::number(completions.toInt())
                                              : parallelism.isDouble()
                                                    ? QString::number(parallelism.toInt())
                                                    : QStringLiteral("-");
                const QString pods = QString("%1/%2").arg(running).arg(requested);

                table->SetColumn(row, 0, metadata["name"].toString());
                table->SetColumn(row, 1, jobStatus);
                table->SetColumn(row, 2, pods, Qt::AlignRight | Qt::AlignVCenter);
                table->SetColumn(row, 3, formatCreated(metadata["creationTimestamp"].toString()));
                table->SetColumn(row, 4, computeAge(metadata["creationTimestamp"].toString()), Qt::AlignRight | Qt::AlignVCenter);
                table->SetHiddenColumn(row, namespaceColumn, metadata["namespace"].toString());
            });
            EventBus::instance().TimerSignal("jobs", timer.elapsed());
        }

        void fetchServices(PageableTable *table, int namespaceColumn) const {
            if (!table) return;
            QElapsedTimer timer;
            timer.start();
            const QJsonArray items = fetchItems(resourceArgs("services"));

            populatePage(table, items, [&](int row, const QJsonObject &svc) {
                const QJsonObject metadata = svc["metadata"].toObject();
                const QJsonObject spec = svc["spec"].toObject();

                table->SetColumn(row, 0, metadata["name"].toString());
                table->SetColumn(row, 1, spec["type"].toString());
                table->SetColumn(row, 2, spec["clusterIP"].toString());
                table->SetColumn(row, 3, formatCreated(metadata["creationTimestamp"].toString()));
                table->SetColumn(row, 4, computeAge(metadata["creationTimestamp"].toString()),
                                 Qt::AlignRight | Qt::AlignVCenter);
                table->SetHiddenColumn(row, namespaceColumn, metadata["namespace"].toString());
            });
            EventBus::instance().TimerSignal("services", timer.elapsed());
        }

        void fetchNamespaces(PageableTable *table) const {
            if (!table) return;
            QElapsedTimer timer;
            timer.start();
            QStringList args = baseArgs();
            args << "get" << "namespaces" << "-o" << "json";
            const QJsonArray items = fetchItems(args);

            populatePage(table, items, [&](int row, const QJsonObject &obj) {
                const QJsonObject metadata = obj["metadata"].toObject();
                table->SetColumn(row, 0, metadata["name"].toString());
                table->SetColumn(row, 1, obj["status"].toObject()["phase"].toString());
                table->SetColumn(row, 2, computeAge(metadata["creationTimestamp"].toString()));
            });
            EventBus::instance().TimerSignal("namespaces", timer.elapsed());
        }

        void fetchNodes(PageableTable *table) const {
            if (!table) return;
            QElapsedTimer timer;
            timer.start();
            // Both kubectl calls below are gathered under one guard so the busy overlay
            // shows/hides at most once for the pair, instead of potentially flickering
            // between them (each kubectl call otherwise manages the overlay on its own).
            BusyGuard busyGuard;
            QStringList args = baseArgs();
            args << "get" << "nodes" << "-o" << "json";
            const QJsonArray items = fetchItems(args);

            QStringList podArgs = baseArgs();
            podArgs << "get" << "pods" << "--all-namespaces" << "-o" << "json";
            QHash<QString, int> podCountByNode;
            QHash<QString, qint64> cpuRequestMillisByNode;
            QHash<QString, qint64> cpuLimitMillisByNode;
            for (const auto &podValue: fetchItems(podArgs)) {
                const QJsonObject pod = podValue.toObject();
                const QString nodeName = pod["spec"].toObject()["nodeName"].toString();
                if (nodeName.isEmpty() || isTerminatedPodPhase(pod["status"].toObject()["phase"].toString())) continue;

                ++podCountByNode[nodeName];
                for (const auto &containerValue: pod["spec"].toObject()["containers"].toArray()) {
                    const QJsonObject resources = containerValue.toObject()["resources"].toObject();
                    cpuRequestMillisByNode[nodeName] += parseCpuMillis(resources["requests"].toObject()["cpu"].toString());
                    cpuLimitMillisByNode[nodeName] += parseCpuMillis(resources["limits"].toObject()["cpu"].toString());
                }
            }

            populatePage(table, items, [&](const int row, const QJsonObject &node) {
                const QJsonObject metadata = node["metadata"].toObject();
                const QString name = metadata["name"].toString();

                QString readyStatus = "Unknown";
                for (const auto &conditionValue: node["status"].toObject()["conditions"].toArray()) {
                    if (const QJsonObject condition = conditionValue.toObject(); condition["type"].toString() == "Ready") {
                        readyStatus = condition["status"].toString() == "True" ? "Ready" : "NotReady";
                    }
                }

                const QString version = node["status"].toObject()["nodeInfo"].toObject()["kubeletVersion"].toString();

                QString internalIp;
                for (const auto &addressValue: node["status"].toObject()["addresses"].toArray()) {
                    if (const QJsonObject address = addressValue.toObject(); address["type"].toString() == "InternalIP") {
                        internalIp = address["address"].toString();
                    }
                }

                const int allocatablePods = node["status"].toObject()["allocatable"].toObject()["pods"].toString().toInt();
                const QString pods = QString("%1/%2").arg(podCountByNode.value(name)).arg(allocatablePods);
                const QString cpuRequests = formatCpuMillis(cpuRequestMillisByNode.value(name));
                const QString cpuLimits = formatCpuMillis(cpuLimitMillisByNode.value(name));
                const QString cpuCapacity = node["status"].toObject()["capacity"].toObject()["cpu"].toString();

                table->SetColumn(row, 0, name);
                table->SetColumn(row, 1, readyStatus);
                table->SetColumn(row, 2, version);
                table->SetColumn(row, 3, internalIp);
                table->SetColumn(row, 4, cpuRequests, Qt::AlignRight | Qt::AlignVCenter);
                table->SetColumn(row, 5, cpuLimits, Qt::AlignRight | Qt::AlignVCenter);
                table->SetColumn(row, 6, cpuCapacity, Qt::AlignRight | Qt::AlignVCenter);
                table->SetColumn(row, 7, pods, Qt::AlignRight | Qt::AlignVCenter);
                table->SetColumn(row, 8, formatCreated(metadata["creationTimestamp"].toString()));
            });
            EventBus::instance().TimerSignal("nodes", timer.elapsed());
        }

        void showTableContextMenu(int pageIndex, const QPoint &pos) {
            const ResourceSpec &spec = specs_[pageIndex];
            auto *table = qobject_cast<PageableTable *>(pages_->widget(pageIndex));
            if (!table || spec.kubectlName.isEmpty()) return;

            const QModelIndex index = table->GetIndexFromPosition(pos);
            if (!index.isValid()) return;

            const auto name = table->GetValue<QString>(index, 0);
            const QString ns = spec.namespaceColumn >= 0 ? table->GetValue<QString>(index, spec.namespaceColumn) : QString();

            const bool supportsLogs = spec.kind == PageKind::Pods || spec.kind == PageKind::Jobs;

            QMenu menu(this);
            const QAction *logsAction =
                supportsLogs ? menu.addAction(IconUtils::GetIcon("logs"), "Logs") : nullptr;
            const QAction *editAction = menu.addAction(IconUtils::GetIcon("edit"), "Edit");
            const QAction *deleteAction = menu.addAction(IconUtils::GetIcon("delete"), "Delete");

            if (const QAction *chosen = menu.exec(table->GetGlobalPosition(pos)); chosen == editAction) {
                editResource(spec.kubectlName, name, ns);
            } else if (chosen == deleteAction) {
                deleteResource(spec.kubectlName, name, ns);
            } else if (chosen == logsAction) {
                showLogsDialog(spec.kubectlName, name, ns);
            }
        }

        // Shows logs for a pod, or (for a job) the pod(s) belonging to that job.
        void showLogsDialog(const QString &resource, const QString &name, const QString &ns) {
            QJsonArray pods;
            QString initialLogs;
            {
                BusyGuard busyGuard;
                if (resource == "pods") {
                    QStringList getArgs = baseArgs();
                    getArgs << "get" << "pods" << name << "-n" << ns << "-o" << "json";
                    const KubectlResult podResult = runKubectlCommand(getArgs);
                    if (!podResult.success) {
                        QMessageBox::warning(this, "Logs failed", podResult.error);
                        return;
                    }
                    pods.append(QJsonDocument::fromJson(podResult.output.toUtf8()).object());
                } else {
                    QStringList podArgs = baseArgs();
                    podArgs << "get" << "pods" << "-n" << ns << "-l" << "job-name=" + name << "-o" << "json";
                    pods = fetchItems(podArgs);
                }
                if (pods.isEmpty()) {
                    QMessageBox::warning(this, "Logs failed", "No pods found for " + resource + "/" + name);
                    return;
                }

                const QJsonObject firstPod = pods[0].toObject();
                const QJsonArray firstContainers = firstPod["spec"].toObject()["containers"].toArray();
                if (!firstContainers.isEmpty()) {
                    QStringList logArgs = baseArgs();
                    logArgs << "logs" << firstPod["metadata"].toObject()["name"].toString() << "-n" << ns << "-c"
                            << firstContainers[0].toObject()["name"].toString() << "--tail=2000";
                    const KubectlResult logResult = runKubectlCommand(logArgs);
                    initialLogs = logResult.success ? logResult.output : "Failed to fetch logs: " + logResult.error;
                }
            }

            QDialog dialog(this);
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
            connect(scrollToStartShortcut, &QShortcut::activated, logView, [logView] {
                logView->moveCursor(QTextCursor::Start);
                logView->ensureCursorVisible();
            });
            auto *scrollToEndShortcut = new QShortcut(QKeySequence("Ctrl+End"), &dialog);
            connect(scrollToEndShortcut, &QShortcut::activated, logView, [logView] {
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

            auto fetchLogs = [this, logView, podBox, containerBox, previousButton, ns, setLogText] {
                if (containerBox->currentText().isEmpty()) return;
                BusyGuard busyGuard;
                QStringList logArgs = baseArgs();
                logArgs << "logs" << podBox->currentText() << "-n" << ns << "-c" << containerBox->currentText()
                        << "--tail=2000";
                if (previousButton->isChecked()) logArgs << "--previous";
                const KubectlResult logResult = runKubectlCommand(logArgs);
                setLogText(logResult.success ? logResult.output : "Failed to fetch logs: " + logResult.error);
            };

            connect(podBox, &QComboBox::currentIndexChanged, this, [updateContainers, fetchLogs](int) {
                updateContainers();
                fetchLogs();
            });
            connect(containerBox, &QComboBox::currentIndexChanged, this, [fetchLogs](int) { fetchLogs(); });
            connect(previousButton, &QToolButton::toggled, this, [fetchLogs](bool) { fetchLogs(); });
            connect(refreshButton, &QPushButton::clicked, this, [fetchLogs] { fetchLogs(); });

            auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(closeButtons);

            setLogText(initialLogs);

            dialog.exec();
        }

        void editResource(const QString &resource, const QString &name, const QString &ns) {
            QStringList getArgs = baseArgs();
            getArgs << "get" << resource << name;
            if (!ns.isEmpty()) getArgs << "-n" << ns;
            getArgs << "-o" << "yaml";

            const KubectlResult getResult = runKubectlCommand(getArgs);
            if (!getResult.success) {
                QMessageBox::warning(this, "Edit failed", getResult.error);
                return;
            }

            QDialog dialog(this);
            dialog.setWindowTitle("Edit " + resource + "/" + name);
            dialog.resize(700, 700);
            auto *layout = new QVBoxLayout(&dialog);
            auto *editor = new QPlainTextEdit(&dialog);
            editor->setPlainText(getResult.output);
            layout->addWidget(editor);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted) return;

            const KubectlResult applyResult = runKubectlCommand(baseArgs() << "apply" << "-f" << "-", editor->toPlainText());
            if (!applyResult.success) {
                QMessageBox::warning(this, "Apply failed", applyResult.error);
            }
            refreshCurrentPage();
        }

        void deleteResource(const QString &resource, const QString &name, const QString &ns) {
            const QString displayName = ns.isEmpty() ? name : ns + "/" + name;
            if (QMessageBox::question(this, "Delete " + resource, "Delete " + resource + " \"" + displayName + "\"?") !=
                QMessageBox::Yes) {
                return;
            }

            QStringList args = baseArgs();
            args << "delete" << resource << name;
            if (!ns.isEmpty()) args << "-n" << ns;

            const KubectlResult result = runKubectlCommand(args);
            if (!result.success) {
                QMessageBox::warning(this, "Delete failed", result.error);
            }
            refreshCurrentPage();
        }

        void showJobDetailsForRow(int pageIndex, const QModelIndex &index) {
            const ResourceSpec &spec = specs_[pageIndex];
            auto *table = qobject_cast<PageableTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;

            const auto name = table->GetValue<QString>(index, 0);
            const auto ns = table->GetValue<QString>(index, spec.namespaceColumn);
            showJobDetails(name, ns);
        }

        void showJobDetails(const QString &name, const QString &ns) {
            QJsonObject job;
            QJsonArray pods;
            QJsonArray events;
            {
                BusyGuard busyGuard;

                QStringList getArgs = baseArgs();
                getArgs << "get" << "jobs" << name << "-n" << ns << "-o" << "json";
                const auto [success, output, error] = runKubectlCommand(getArgs);
                if (!success) {
                    QMessageBox::warning(this, "Job details failed", error);
                    return;
                }
                job = QJsonDocument::fromJson(output.toUtf8()).object();

                QStringList podArgs = baseArgs();
                podArgs << "get" << "pods" << "-n" << ns << "-l" << "job-name=" + name << "-o" << "json";
                pods = fetchItems(podArgs);

                QStringList eventArgs = baseArgs();
                eventArgs << "get" << "events" << "-n" << ns << "--field-selector"
                        << "involvedObject.name=" + name + ",involvedObject.kind=Job" << "-o" << "json";
                events = fetchItems(eventArgs);
            }

            const QJsonObject metadata = job["metadata"].toObject();
            const QJsonObject status = job["status"].toObject();
            const QJsonObject spec = job["spec"].toObject();
            const QJsonValue completions = spec["completions"];
            const QJsonValue parallelism = spec["parallelism"];
            const QString desired = completions.isDouble()
                                        ? QString::number(completions.toInt())
                                        : parallelism.isDouble()
                                              ? QString::number(parallelism.toInt())
                                              : QStringLiteral("-");

            QDialog dialog(this);
            dialog.setWindowTitle("Job: " + ns + "/" + name);
            dialog.resize(1100, 800);

            auto *content = new QWidget();
            auto *layout = new QVBoxLayout(content);

            auto *metaBox = new QGroupBox("Metadata");
            auto *metaForm = new QFormLayout(metaBox);
            metaForm->addRow("Name", new QLabel(metadata["name"].toString()));
            metaForm->addRow("Namespace", new QLabel(metadata["namespace"].toString()));
            metaForm->addRow("Created", new QLabel(formatCreated(metadata["creationTimestamp"].toString())));
            metaForm->addRow("Age", new QLabel(computeAge(metadata["creationTimestamp"].toString())));
            metaForm->addRow("UID", new QLabel(metadata["uid"].toString()));
            auto *labelsValue = new QLabel(joinKeyValues(metadata["labels"].toObject()));
            labelsValue->setWordWrap(true);
            metaForm->addRow("Labels", labelsValue);
            auto *annotationsValue = new QLabel(joinKeyValues(metadata["annotations"].toObject()));
            annotationsValue->setWordWrap(true);
            metaForm->addRow("Annotations", annotationsValue);
            layout->addWidget(metaBox);

            auto *resourceBox = new QGroupBox("Resource information");
            auto *resourceForm = new QFormLayout(resourceBox);
            resourceForm->addRow("Completions", new QLabel(completions.isDouble() ? QString::number(completions.toInt()) : "-"));
            resourceForm->addRow("Parallelism", new QLabel(parallelism.isDouble() ? QString::number(parallelism.toInt()) : "-"));
            QStringList images;
            for (const auto &containerValue: spec["template"].toObject()["spec"].toObject()["containers"].toArray()) {
                images << containerValue.toObject()["image"].toString();
            }
            auto *imagesLabel = new QLabel(images.join("\n"));
            imagesLabel->setWordWrap(true);
            resourceForm->addRow("Images", imagesLabel);
            layout->addWidget(resourceBox);

            auto *conditionsBox = new QGroupBox("Conditions");
            auto *conditionsLayout = new QVBoxLayout(conditionsBox);
            auto *conditionsTable =
                    makeDetailTable({"Type", "Status", "Last Probe", "Last Transition", "Reason", "Message"});
            const QJsonArray conditions = status["conditions"].toArray();
            conditionsTable->setRowCount(conditions.size());
            for (int i = 0; i < conditions.size(); ++i) {
                const QJsonObject condition = conditions[i].toObject();
                conditionsTable->setItem(i, 0, new QTableWidgetItem(condition["type"].toString()));
                conditionsTable->setItem(i, 1, new QTableWidgetItem(condition["status"].toString()));
                conditionsTable->setItem(i, 2, new QTableWidgetItem(formatCreated(condition["lastProbeTime"].toString())));
                conditionsTable->setItem(i, 3,
                                         new QTableWidgetItem(formatCreated(condition["lastTransitionTime"].toString())));
                conditionsTable->setItem(i, 4, new QTableWidgetItem(condition["reason"].toString()));
                conditionsTable->setItem(i, 5, new QTableWidgetItem(condition["message"].toString()));
            }
            conditionsLayout->addWidget(conditionsTable);
            layout->addWidget(conditionsBox);

            auto *podsStatusBox = new QGroupBox("Pods status");
            auto *podsStatusForm = new QFormLayout(podsStatusBox);
            podsStatusForm->addRow("Running", new QLabel(QString::number(status["active"].toInt())));
            podsStatusForm->addRow("Desired", new QLabel(desired));
            layout->addWidget(podsStatusBox);

            auto *podsBox = new QGroupBox(QString("Pods (%1)").arg(pods.size()));
            auto *podsLayout = new QVBoxLayout(podsBox);
            auto *podsTable = makeDetailTable({"Name", "Images", "Node", "Status", "Restarts", "Created"});
            podsTable->setRowCount(pods.size());
            for (int i = 0; i < pods.size(); ++i) {
                const QJsonObject pod = pods[i].toObject();
                const QJsonObject podMeta = pod["metadata"].toObject();
                const QJsonObject podStatus = pod["status"].toObject();
                const QJsonObject podSpec = pod["spec"].toObject();

                QStringList podImages;
                for (const auto &containerValue: podSpec["containers"].toArray()) {
                    podImages << containerValue.toObject()["image"].toString();
                }
                int restarts = 0;
                for (const auto &containerStatusValue: podStatus["containerStatuses"].toArray()) {
                    restarts += containerStatusValue.toObject()["restartCount"].toInt();
                }

                podsTable->setItem(i, 0, new QTableWidgetItem(podMeta["name"].toString()));
                podsTable->setItem(i, 1, new QTableWidgetItem(podImages.join(", ")));
                podsTable->setItem(i, 2, new QTableWidgetItem(podSpec["nodeName"].toString()));
                podsTable->setItem(i, 3, new QTableWidgetItem(podStatus["phase"].toString()));
                podsTable->setItem(i, 4, new QTableWidgetItem(QString::number(restarts)));
                podsTable->setItem(i, 5, new QTableWidgetItem(computeAge(podMeta["creationTimestamp"].toString())));
            }
            podsLayout->addWidget(podsTable);
            layout->addWidget(podsBox);

            auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
            auto *eventsLayout = new QVBoxLayout(eventsBox);
            auto *eventsTable = makeDetailTable({"Type", "Reason", "Message", "Age"});
            eventsTable->setRowCount(events.size());
            for (int i = 0; i < events.size(); ++i) {
                const QJsonObject event = events[i].toObject();
                eventsTable->setItem(i, 0, new QTableWidgetItem(event["type"].toString()));
                eventsTable->setItem(i, 1, new QTableWidgetItem(event["reason"].toString()));
                eventsTable->setItem(i, 2, new QTableWidgetItem(event["message"].toString()));
                eventsTable->setItem(i, 3, new QTableWidgetItem(computeAge(event["lastTimestamp"].toString())));
            }
            eventsLayout->addWidget(eventsTable);
            layout->addWidget(eventsBox);

            auto *scrollArea = new QScrollArea(&dialog);
            scrollArea->setWidgetResizable(true);
            scrollArea->setWidget(content);

            auto *dialogLayout = new QVBoxLayout(&dialog);
            dialogLayout->addWidget(scrollArea);

            dialog.exec();
        }

        void showPodDetailsForRow(int pageIndex, const QModelIndex &index) {
            const ResourceSpec &spec = specs_[pageIndex];
            auto *table = qobject_cast<PageableTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;

            const auto name = table->GetValue<QString>(index, 0);
            const auto ns = table->GetValue<QString>(index, spec.namespaceColumn);
            showPodDetails(name, ns);
        }

        void showPodDetails(const QString &name, const QString &ns) {
            QJsonObject pod;
            QJsonArray events;
            QString ownerKind;
            QString ownerName;
            KubectlResult ownerResult;
            {
                BusyGuard busyGuard;

                QStringList getArgs = baseArgs();
                getArgs << "get" << "pods" << name << "-n" << ns << "-o" << "json";
                const auto [success, output, error] = runKubectlCommand(getArgs);
                if (!success) {
                    QMessageBox::warning(this, "Pod details failed", error);
                    return;
                }
                pod = QJsonDocument::fromJson(output.toUtf8()).object();

                QStringList eventArgs = baseArgs();
                eventArgs << "get" << "events" << "-n" << ns << "--field-selector"
                        << "involvedObject.name=" + name + ",involvedObject.kind=Pod" << "-o" << "json";
                events = fetchItems(eventArgs);

                if (const QJsonArray ownerReferences = pod["metadata"].toObject()["ownerReferences"].toArray(); !ownerReferences.isEmpty()) {
                    const QJsonObject owner = ownerReferences[0].toObject();
                    ownerKind = owner["kind"].toString();
                    ownerName = owner["name"].toString();

                    QStringList ownerArgs = baseArgs();
                    ownerArgs << "get" << (ownerKind.toLower() + "s") << ownerName << "-n" << ns << "-o" << "json";
                    ownerResult = runKubectlCommand(ownerArgs);
                }
            }

            const QJsonObject metadata = pod["metadata"].toObject();
            const QJsonObject status = pod["status"].toObject();
            const QJsonObject spec = pod["spec"].toObject();

            QDialog dialog(this);
            dialog.setWindowTitle("Pod: " + ns + "/" + name);
            dialog.resize(1100, 800);

            auto *content = new QWidget();
            auto *layout = new QVBoxLayout(content);

            auto *metaBox = new QGroupBox("Metadata");
            auto *metaForm = new QFormLayout(metaBox);
            metaForm->addRow("Name", new QLabel(metadata["name"].toString()));
            metaForm->addRow("Namespace", new QLabel(metadata["namespace"].toString()));
            metaForm->addRow("Created", new QLabel(formatCreated(metadata["creationTimestamp"].toString())));
            metaForm->addRow("Age", new QLabel(computeAge(metadata["creationTimestamp"].toString())));
            metaForm->addRow("UID", new QLabel(metadata["uid"].toString()));
            auto *labelsValue = new QLabel(joinKeyValues(metadata["labels"].toObject()));
            labelsValue->setWordWrap(true);
            metaForm->addRow("Labels", labelsValue);
            auto *annotationsValue = new QLabel(joinKeyValues(metadata["annotations"].toObject()));
            annotationsValue->setWordWrap(true);
            metaForm->addRow("Annotations", annotationsValue);
            layout->addWidget(metaBox);

            auto *resourceBox = new QGroupBox("Resource information");
            auto *resourceForm = new QFormLayout(resourceBox);
            resourceForm->addRow("Node", new QLabel(spec["nodeName"].toString()));
            resourceForm->addRow("Status", new QLabel(status["phase"].toString()));
            resourceForm->addRow("Pod IP", new QLabel(status["podIP"].toString()));
            resourceForm->addRow("QoS Class", new QLabel(status["qosClass"].toString()));
            int totalRestarts = 0;
            for (const auto &containerStatusValue: status["containerStatuses"].toArray()) {
                totalRestarts += containerStatusValue.toObject()["restartCount"].toInt();
            }
            resourceForm->addRow("Restarts", new QLabel(QString::number(totalRestarts)));
            resourceForm->addRow("Service Account", new QLabel(spec["serviceAccountName"].toString()));
            QStringList pullSecrets;
            for (const auto &secretValue: spec["imagePullSecrets"].toArray()) {
                pullSecrets << secretValue.toObject()["name"].toString();
            }
            resourceForm->addRow("Image Pull Secrets", new QLabel(pullSecrets.isEmpty() ? "-" : pullSecrets.join(", ")));
            layout->addWidget(resourceBox);

            auto *conditionsBox = new QGroupBox("Conditions");
            auto *conditionsLayout = new QVBoxLayout(conditionsBox);
            auto *conditionsTable = makeDetailTable({"Type", "Status", "Last Probe", "Last Transition", "Reason", "Message"});
            const QJsonArray conditions = status["conditions"].toArray();
            conditionsTable->setRowCount(conditions.size());
            for (int i = 0; i < conditions.size(); ++i) {
                const QJsonObject condition = conditions[i].toObject();
                conditionsTable->setItem(i, 0, new QTableWidgetItem(condition["type"].toString()));
                conditionsTable->setItem(i, 1, new QTableWidgetItem(condition["status"].toString()));
                conditionsTable->setItem(i, 2, new QTableWidgetItem(formatCreated(condition["lastProbeTime"].toString())));
                conditionsTable->setItem(i, 3,
                                         new QTableWidgetItem(formatCreated(condition["lastTransitionTime"].toString())));
                conditionsTable->setItem(i, 4, new QTableWidgetItem(condition["reason"].toString()));
                conditionsTable->setItem(i, 5, new QTableWidgetItem(condition["message"].toString()));
            }
            conditionsLayout->addWidget(conditionsTable);
            layout->addWidget(conditionsBox);

            auto *controlledByBox = new QGroupBox("Controlled by");
            auto *controlledByLayout = new QVBoxLayout(controlledByBox);
            if (ownerName.isEmpty()) {
                controlledByLayout->addWidget(new QLabel("Not controlled by another resource."));
            } else {
                auto *ownerForm = new QFormLayout();
                ownerForm->addRow("Name", new QLabel(ownerName));
                ownerForm->addRow("Kind", new QLabel(ownerKind));

                if (ownerResult.success) {
                    const QJsonObject ownerObj = QJsonDocument::fromJson(ownerResult.output.toUtf8()).object();
                    const QJsonObject ownerMeta = ownerObj["metadata"].toObject();
                    const QJsonObject ownerStatus = ownerObj["status"].toObject();
                    const QJsonObject ownerSpec = ownerObj["spec"].toObject();

                    const QString desired = ownerSpec.contains("replicas")
                                                ? QString::number(ownerSpec["replicas"].toInt())
                                                : ownerSpec.contains("completions")
                                                      ? QString::number(ownerSpec["completions"].toInt())
                                                      : "-";
                    const QString ready = ownerStatus.contains("readyReplicas")
                                              ? QString::number(ownerStatus["readyReplicas"].toInt())
                                              : ownerStatus.contains("succeeded")
                                                    ? QString::number(ownerStatus["succeeded"].toInt())
                                                    : "-";
                    ownerForm->addRow("Pods", new QLabel(ready + " / " + desired));
                    ownerForm->addRow("Age", new QLabel(computeAge(ownerMeta["creationTimestamp"].toString())));

                    auto *ownerLabelsValue = new QLabel(joinKeyValues(ownerMeta["labels"].toObject()));
                    ownerLabelsValue->setWordWrap(true);
                    ownerForm->addRow("Labels", ownerLabelsValue);

                    QStringList ownerImages;
                    for (const auto &containerValue:
                         ownerSpec["template"].toObject()["spec"].toObject()["containers"].toArray()) {
                        ownerImages << containerValue.toObject()["image"].toString();
                    }
                    auto *ownerImagesLabel = new QLabel(ownerImages.join("\n"));
                    ownerImagesLabel->setWordWrap(true);
                    ownerForm->addRow("Images", ownerImagesLabel);
                }
                controlledByLayout->addLayout(ownerForm);
            }
            layout->addWidget(controlledByBox);

            auto *pvcBox = new QGroupBox("Persistent Volume Claims");
            auto *pvcLayout = new QVBoxLayout(pvcBox);
            QStringList pvcNames;
            for (const auto &volumeValue: spec["volumes"].toArray()) {
                if (const QJsonObject volume = volumeValue.toObject(); volume.contains("persistentVolumeClaim")) {
                    pvcNames << volume["persistentVolumeClaim"].toObject()["claimName"].toString();
                }
            }
            if (pvcNames.isEmpty()) {
                pvcLayout->addWidget(new QLabel("There is nothing to display here."));
            } else {
                auto *pvcTable = makeDetailTable({"Claim Name"});
                pvcTable->setRowCount(pvcNames.size());
                for (int i = 0; i < pvcNames.size(); ++i) {
                    pvcTable->setItem(i, 0, new QTableWidgetItem(pvcNames[i]));
                }
                pvcLayout->addWidget(pvcTable);
            }
            layout->addWidget(pvcBox);

            auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
            auto *eventsLayout = new QVBoxLayout(eventsBox);
            auto *eventsTable = makeDetailTable({"Reason", "Message", "Count", "First Seen", "Last Seen"});
            eventsTable->setRowCount(events.size());
            for (int i = 0; i < events.size(); ++i) {
                const QJsonObject event = events[i].toObject();
                eventsTable->setItem(i, 0, new QTableWidgetItem(event["reason"].toString()));
                eventsTable->setItem(i, 1, new QTableWidgetItem(event["message"].toString()));
                eventsTable->setItem(i, 2, new QTableWidgetItem(QString::number(event["count"].toInt())));
                eventsTable->setItem(i, 3, new QTableWidgetItem(computeAge(event["firstTimestamp"].toString())));
                eventsTable->setItem(i, 4, new QTableWidgetItem(computeAge(event["lastTimestamp"].toString())));
            }
            eventsLayout->addWidget(eventsTable);
            layout->addWidget(eventsBox);

            const QJsonArray containerStatuses = status["containerStatuses"].toArray();
            const QJsonArray volumes = spec["volumes"].toArray();
            auto *containersBox = new QGroupBox("Containers");
            auto *containersLayout = new QVBoxLayout(containersBox);
            for (const auto &containerValue: spec["containers"].toArray()) {
                const QJsonObject containerSpec = containerValue.toObject();
                const QJsonObject containerStatus = findByName(containerStatuses, containerSpec["name"].toString());
                containersLayout->addWidget(buildContainerBox(containerSpec, containerStatus, volumes));
            }
            layout->addWidget(containersBox);

            auto *scrollArea = new QScrollArea(&dialog);
            scrollArea->setWidgetResizable(true);
            scrollArea->setWidget(content);

            auto *dialogLayout = new QVBoxLayout(&dialog);
            dialogLayout->addWidget(scrollArea);

            dialog.exec();
        }

        void showServiceDetailsForRow(int pageIndex, const QModelIndex &index) {
            const ResourceSpec &spec = specs_[pageIndex];
            auto *table = qobject_cast<PageableTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;

            const auto name = table->GetValue<QString>(index, 0);
            const auto ns = table->GetValue<QString>(index, spec.namespaceColumn);
            showServiceDetails(name, ns);
        }

        void showServiceDetails(const QString &name, const QString &ns) {
            QJsonObject service;
            QJsonArray endpointSubsets;
            QJsonArray pods;
            QJsonArray matchingIngresses;
            QJsonArray events;
            {
                BusyGuard busyGuard;

                QStringList getArgs = baseArgs();
                getArgs << "get" << "services" << name << "-n" << ns << "-o" << "json";
                const KubectlResult serviceResult = runKubectlCommand(getArgs);
                if (!serviceResult.success) {
                    QMessageBox::warning(this, "Service details failed", serviceResult.error);
                    return;
                }
                service = QJsonDocument::fromJson(serviceResult.output.toUtf8()).object();

                QStringList endpointArgs = baseArgs();
                endpointArgs << "get" << "endpoints" << name << "-n" << ns << "-o" << "json";
                const KubectlResult endpointResult = runKubectlCommand(endpointArgs);
                if (endpointResult.success) {
                    endpointSubsets = QJsonDocument::fromJson(endpointResult.output.toUtf8()).object()["subsets"].toArray();
                }

                if (const QJsonObject selector = service["spec"].toObject()["selector"].toObject(); !selector.isEmpty()) {
                    QStringList selectorParts;
                    for (auto it = selector.begin(); it != selector.end(); ++it) {
                        selectorParts << it.key() + "=" + it.value().toString();
                    }
                    QStringList podArgs = baseArgs();
                    podArgs << "get" << "pods" << "-n" << ns << "-l" << selectorParts.join(",") << "-o" << "json";
                    pods = fetchItems(podArgs);
                }

                QStringList ingressArgs = baseArgs();
                ingressArgs << "get" << "ingresses" << "-n" << ns << "-o" << "json";
                for (const auto &ingressValue: fetchItems(ingressArgs)) {
                    const QJsonObject ingress = ingressValue.toObject();
                    const QJsonObject ingressSpec = ingress["spec"].toObject();
                    bool referencesService =
                            ingressSpec["defaultBackend"].toObject()["service"].toObject()["name"].toString() == name;
                    for (const auto &ruleValue: ingressSpec["rules"].toArray()) {
                        for (const auto &pathValue: ruleValue.toObject()["http"].toObject()["paths"].toArray()) {
                            if (pathValue.toObject()["backend"].toObject()["service"].toObject()["name"].toString() ==
                                name) {
                                referencesService = true;
                            }
                        }
                    }
                    if (referencesService) {
                        matchingIngresses.append(ingress);
                    }
                }

                QStringList eventArgs = baseArgs();
                eventArgs << "get" << "events" << "-n" << ns << "--field-selector"
                        << "involvedObject.name=" + name + ",involvedObject.kind=Service" << "-o" << "json";
                events = fetchItems(eventArgs);
            }

            const QJsonObject metadata = service["metadata"].toObject();
            const QJsonObject spec = service["spec"].toObject();

            QDialog dialog(this);
            dialog.setWindowTitle("Service: " + ns + "/" + name);
            dialog.resize(1100, 800);

            auto *content = new QWidget();
            auto *layout = new QVBoxLayout(content);

            auto *metaBox = new QGroupBox("Metadata");
            auto *metaForm = new QFormLayout(metaBox);
            metaForm->addRow("Name", new QLabel(metadata["name"].toString()));
            metaForm->addRow("Namespace", new QLabel(metadata["namespace"].toString()));
            metaForm->addRow("Created", new QLabel(formatCreated(metadata["creationTimestamp"].toString())));
            metaForm->addRow("Age", new QLabel(computeAge(metadata["creationTimestamp"].toString())));
            metaForm->addRow("UID", new QLabel(metadata["uid"].toString()));
            auto *labelsValue = new QLabel(joinKeyValues(metadata["labels"].toObject()));
            labelsValue->setWordWrap(true);
            metaForm->addRow("Labels", labelsValue);
            auto *annotationsValue = new QLabel(joinKeyValues(metadata["annotations"].toObject()));
            annotationsValue->setWordWrap(true);
            metaForm->addRow("Annotations", annotationsValue);
            layout->addWidget(metaBox);

            auto *resourceBox = new QGroupBox("Resource information");
            auto *resourceForm = new QFormLayout(resourceBox);
            resourceForm->addRow("Type", new QLabel(spec["type"].toString()));
            resourceForm->addRow("Cluster IP", new QLabel(spec["clusterIP"].toString()));
            resourceForm->addRow("Session Affinity", new QLabel(spec["sessionAffinity"].toString()));
            auto *selectorValue = new QLabel(joinKeyValues(spec["selector"].toObject()));
            selectorValue->setWordWrap(true);
            resourceForm->addRow("Selector", selectorValue);
            layout->addWidget(resourceBox);

            auto *endpointsBox = new QGroupBox("Endpoints");
            auto *endpointsLayout = new QVBoxLayout(endpointsBox);
            QList<QStringList> endpointRows;
            for (const auto &subsetValue: endpointSubsets) {
                const QJsonObject subset = subsetValue.toObject();
                QStringList ips;
                for (const auto &addressValue: subset["addresses"].toArray()) {
                    ips << addressValue.toObject()["ip"].toString();
                }
                for (const auto &portValue: subset["ports"].toArray()) {
                    const QJsonObject port = portValue.toObject();
                    for (const QString &ip: ips) {
                        endpointRows.append(
                            {ip, QString::number(port["port"].toInt()), port["protocol"].toString()});
                    }
                }
            }
            if (endpointRows.isEmpty()) {
                auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
                emptyLabel->setAlignment(Qt::AlignCenter);
                endpointsLayout->addWidget(emptyLabel);
            } else {
                auto *endpointsTable = makeDetailTable({"IP", "Port", "Protocol"});
                endpointsTable->setRowCount(endpointRows.size());
                for (int i = 0; i < endpointRows.size(); ++i) {
                    endpointsTable->setItem(i, 0, new QTableWidgetItem(endpointRows[i][0]));
                    endpointsTable->setItem(i, 1, new QTableWidgetItem(endpointRows[i][1]));
                    endpointsTable->setItem(i, 2, new QTableWidgetItem(endpointRows[i][2]));
                }
                endpointsLayout->addWidget(endpointsTable);
            }
            layout->addWidget(endpointsBox);

            auto *podsBox = new QGroupBox("Pods");
            auto *podsLayout = new QVBoxLayout(podsBox);
            if (pods.isEmpty()) {
                auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
                emptyLabel->setAlignment(Qt::AlignCenter);
                podsLayout->addWidget(emptyLabel);
            } else {
                auto *podsTable = makeDetailTable({"Name", "Images", "Node", "Status", "Restarts", "Created"});
                podsTable->setRowCount(pods.size());
                for (int i = 0; i < pods.size(); ++i) {
                    const QJsonObject pod = pods[i].toObject();
                    const QJsonObject podMeta = pod["metadata"].toObject();
                    const QJsonObject podStatus = pod["status"].toObject();
                    const QJsonObject podSpec = pod["spec"].toObject();

                    QStringList podImages;
                    for (const auto &containerValue: podSpec["containers"].toArray()) {
                        podImages << containerValue.toObject()["image"].toString();
                    }
                    int restarts = 0;
                    for (const auto &containerStatusValue: podStatus["containerStatuses"].toArray()) {
                        restarts += containerStatusValue.toObject()["restartCount"].toInt();
                    }

                    podsTable->setItem(i, 0, new QTableWidgetItem(podMeta["name"].toString()));
                    podsTable->setItem(i, 1, new QTableWidgetItem(podImages.join(", ")));
                    podsTable->setItem(i, 2, new QTableWidgetItem(podSpec["nodeName"].toString()));
                    podsTable->setItem(i, 3, new QTableWidgetItem(podStatus["phase"].toString()));
                    podsTable->setItem(i, 4, new QTableWidgetItem(QString::number(restarts)));
                    podsTable->setItem(i, 5, new QTableWidgetItem(computeAge(podMeta["creationTimestamp"].toString())));
                }
                podsLayout->addWidget(podsTable);
            }
            layout->addWidget(podsBox);

            auto *ingressesBox = new QGroupBox("Ingresses");
            auto *ingressesLayout = new QVBoxLayout(ingressesBox);
            if (matchingIngresses.isEmpty()) {
                auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
                emptyLabel->setAlignment(Qt::AlignCenter);
                ingressesLayout->addWidget(emptyLabel);
            } else {
                auto *ingressesTable = makeDetailTable({"Name", "Hosts", "Created"});
                ingressesTable->setRowCount(matchingIngresses.size());
                for (int i = 0; i < matchingIngresses.size(); ++i) {
                    const QJsonObject ingress = matchingIngresses[i].toObject();
                    const QJsonObject ingressMeta = ingress["metadata"].toObject();
                    QStringList hosts;
                    for (const auto &ruleValue: ingress["spec"].toObject()["rules"].toArray()) {
                        hosts << ruleValue.toObject()["host"].toString();
                    }
                    ingressesTable->setItem(i, 0, new QTableWidgetItem(ingressMeta["name"].toString()));
                    ingressesTable->setItem(i, 1, new QTableWidgetItem(hosts.join(", ")));
                    ingressesTable->setItem(
                        i, 2, new QTableWidgetItem(computeAge(ingressMeta["creationTimestamp"].toString())));
                }
                ingressesLayout->addWidget(ingressesTable);
            }
            layout->addWidget(ingressesBox);

            auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
            auto *eventsLayout = new QVBoxLayout(eventsBox);
            auto *eventsTable = makeDetailTable({"Type", "Reason", "Message", "Age"});
            eventsTable->setRowCount(events.size());
            for (int i = 0; i < events.size(); ++i) {
                const QJsonObject event = events[i].toObject();
                eventsTable->setItem(i, 0, new QTableWidgetItem(event["type"].toString()));
                eventsTable->setItem(i, 1, new QTableWidgetItem(event["reason"].toString()));
                eventsTable->setItem(i, 2, new QTableWidgetItem(event["message"].toString()));
                eventsTable->setItem(i, 3, new QTableWidgetItem(computeAge(event["lastTimestamp"].toString())));
            }
            eventsLayout->addWidget(eventsTable);
            layout->addWidget(eventsBox);

            auto *scrollArea = new QScrollArea(&dialog);
            scrollArea->setWidgetResizable(true);
            scrollArea->setWidget(content);

            auto *dialogLayout = new QVBoxLayout(&dialog);
            dialogLayout->addWidget(scrollArea);

            dialog.exec();
        }

        void showIngressDetailsForRow(int pageIndex, const QModelIndex &index) {
            const ResourceSpec &spec = specs_[pageIndex];
            auto *table = qobject_cast<PageableTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;

            const auto name = table->GetValue<QString>(index, 0);
            const QString ns = table->GetValue<QString>(index, spec.namespaceColumn);
            showIngressDetails(name, ns);
        }

        void showIngressDetails(const QString &name, const QString &ns) {
            QJsonObject ingress;
            QJsonArray events;
            {
                BusyGuard busyGuard;
                QStringList getArgs = baseArgs();
                getArgs << "get" << "ingresses" << name << "-n" << ns << "-o" << "json";
                const KubectlResult ingressResult = runKubectlCommand(getArgs);
                if (!ingressResult.success) {
                    QMessageBox::warning(this, "Ingress details failed", ingressResult.error);
                    return;
                }
                ingress = QJsonDocument::fromJson(ingressResult.output.toUtf8()).object();

                QStringList eventArgs = baseArgs();
                eventArgs << "get" << "events" << "-n" << ns << "--field-selector"
                          << "involvedObject.name=" + name + ",involvedObject.kind=Ingress" << "-o" << "json";
                events = fetchItems(eventArgs);
            }

            const QJsonObject metadata = ingress["metadata"].toObject();
            const QJsonObject spec = ingress["spec"].toObject();
            const QJsonObject status = ingress["status"].toObject();

            QDialog dialog(this);
            dialog.setWindowTitle("Ingress: " + ns + "/" + name);
            dialog.resize(1100, 800);

            auto *content = new QWidget();
            auto *layout = new QVBoxLayout(content);

            auto *metaBox = new QGroupBox("Metadata");
            auto *metaForm = new QFormLayout(metaBox);
            metaForm->addRow("Name", new QLabel(metadata["name"].toString()));
            metaForm->addRow("Namespace", new QLabel(metadata["namespace"].toString()));
            metaForm->addRow("Created", new QLabel(formatCreated(metadata["creationTimestamp"].toString())));
            metaForm->addRow("Age", new QLabel(computeAge(metadata["creationTimestamp"].toString())));
            metaForm->addRow("UID", new QLabel(metadata["uid"].toString()));
            auto *labelsValue = new QLabel(joinKeyValues(metadata["labels"].toObject()));
            labelsValue->setWordWrap(true);
            metaForm->addRow("Labels", labelsValue);
            auto *annotationsValue = new QLabel(joinKeyValues(metadata["annotations"].toObject()));
            annotationsValue->setWordWrap(true);
            metaForm->addRow("Annotations", annotationsValue);
            layout->addWidget(metaBox);

            auto *resourceBox = new QGroupBox("Resource information");
            auto *resourceForm = new QFormLayout(resourceBox);
            resourceForm->addRow("Ingress Class Name", new QLabel(spec["ingressClassName"].toString()));
            QStringList endpoints;
            for (const auto &lbValue: status["loadBalancer"].toObject()["ingress"].toArray()) {
                const QJsonObject lb = lbValue.toObject();
                const QString host = lb["hostname"].toString();
                endpoints << (!host.isEmpty() ? host : lb["ip"].toString());
            }
            auto *endpointsLabel = new QLabel(endpoints.isEmpty() ? "-" : endpoints.join("\n"));
            endpointsLabel->setWordWrap(true);
            resourceForm->addRow("Endpoints", endpointsLabel);
            layout->addWidget(resourceBox);

            auto *rulesBox = new QGroupBox("Rules");
            auto *rulesLayout = new QVBoxLayout(rulesBox);

            QHash<QString, QString> tlsSecretByHost;
            for (const auto &tlsValue: spec["tls"].toArray()) {
                const QJsonObject tls = tlsValue.toObject();
                const QString secretName = tls["secretName"].toString();
                for (const auto &hostValue: tls["hosts"].toArray()) {
                    tlsSecretByHost[hostValue.toString()] = secretName;
                }
            }

            struct RuleRow {
                QString host, path, pathType, serviceName, servicePort, tlsSecret;
            };
            QList<RuleRow> ruleRows;
            for (const auto &ruleValue: spec["rules"].toArray()) {
                const QJsonObject rule = ruleValue.toObject();
                const QString host = rule["host"].toString();
                for (const auto &pathValue: rule["http"].toObject()["paths"].toArray()) {
                    const QJsonObject pathObj = pathValue.toObject();
                    const QJsonObject backendService = pathObj["backend"].toObject()["service"].toObject();
                    const QJsonValue portValue = backendService["port"].toObject()["number"];
                    ruleRows.append({host, pathObj["path"].toString(), pathObj["pathType"].toString(),
                                      backendService["name"].toString(),
                                      portValue.isDouble() ? QString::number(portValue.toInt()) : QStringLiteral("-"),
                                      tlsSecretByHost.value(host, "-")});
                }
            }

            if (ruleRows.isEmpty()) {
                auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
                emptyLabel->setAlignment(Qt::AlignCenter);
                rulesLayout->addWidget(emptyLabel);
            } else {
                auto *rulesTable = makeDetailTable({"Host", "Path", "Path Type", "Service Name", "Service Port", "TLS Secret"});
                rulesTable->setRowCount(ruleRows.size());
                for (int i = 0; i < ruleRows.size(); ++i) {
                    const RuleRow &r = ruleRows[i];
                    rulesTable->setItem(i, 0, new QTableWidgetItem(r.host));
                    rulesTable->setItem(i, 1, new QTableWidgetItem(r.path));
                    rulesTable->setItem(i, 2, new QTableWidgetItem(r.pathType));
                    rulesTable->setItem(i, 3, new QTableWidgetItem(r.serviceName));
                    rulesTable->setItem(i, 4, new QTableWidgetItem(r.servicePort));
                    rulesTable->setItem(i, 5, new QTableWidgetItem(r.tlsSecret));
                }
                rulesLayout->addWidget(rulesTable);
            }
            layout->addWidget(rulesBox);

            auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
            auto *eventsLayout = new QVBoxLayout(eventsBox);
            auto *eventsTable = makeDetailTable({"Type", "Reason", "Message", "Age"});
            eventsTable->setRowCount(events.size());
            for (int i = 0; i < events.size(); ++i) {
                const QJsonObject event = events[i].toObject();
                eventsTable->setItem(i, 0, new QTableWidgetItem(event["type"].toString()));
                eventsTable->setItem(i, 1, new QTableWidgetItem(event["reason"].toString()));
                eventsTable->setItem(i, 2, new QTableWidgetItem(event["message"].toString()));
                eventsTable->setItem(i, 3, new QTableWidgetItem(computeAge(event["lastTimestamp"].toString())));
            }
            eventsLayout->addWidget(eventsTable);
            layout->addWidget(eventsBox);

            auto *scrollArea = new QScrollArea(&dialog);
            scrollArea->setWidgetResizable(true);
            scrollArea->setWidget(content);

            auto *dialogLayout = new QVBoxLayout(&dialog);
            dialogLayout->addWidget(scrollArea);

            dialog.exec();
        }

        void showNodeDetailsForRow(int pageIndex, const QModelIndex &index) {
            auto *table = qobject_cast<PageableTable *>(pages_->widget(pageIndex));
            if (!table || !index.isValid()) return;

            const auto name = table->GetValue<QString>(index, 0);
            showNodeDetails(name);
        }

        void showNodeDetails(const QString &name) {
            QJsonObject node;
            QJsonArray pods;
            QJsonArray events;
            {
                BusyGuard busyGuard;
                QStringList getArgs = baseArgs();
                getArgs << "get" << "nodes" << name << "-o" << "json";
                const KubectlResult nodeResult = runKubectlCommand(getArgs);
                if (!nodeResult.success) {
                    QMessageBox::warning(this, "Node details failed", nodeResult.error);
                    return;
                }
                node = QJsonDocument::fromJson(nodeResult.output.toUtf8()).object();

                QStringList podArgs = baseArgs();
                podArgs << "get" << "pods" << "--all-namespaces" << "--field-selector" << "spec.nodeName=" + name << "-o" << "json";
                // Excludes Succeeded/Failed pods (e.g. completed Job pods still lingering
                // on the node) to match kubectl describe node / the Kubernetes Dashboard.
                for (const auto &podValue: fetchItems(podArgs)) {
                    if (!isTerminatedPodPhase(podValue.toObject()["status"].toObject()["phase"].toString())) {
                        pods.append(podValue);
                    }
                }

                QStringList eventArgs = baseArgs();
                eventArgs << "get" << "events" << "--all-namespaces" << "--field-selector"
                          << "involvedObject.name=" + name + ",involvedObject.kind=Node" << "-o" << "json";
                events = fetchItems(eventArgs);
            }

            const QJsonObject metadata = node["metadata"].toObject();
            const QJsonObject spec = node["spec"].toObject();
            const QJsonObject status = node["status"].toObject();
            const QJsonObject nodeInfo = status["nodeInfo"].toObject();
            const QJsonObject capacity = status["capacity"].toObject();
            const QJsonObject allocatable = status["allocatable"].toObject();

            QDialog dialog(this);
            dialog.setWindowTitle("Node: " + name);
            dialog.resize(1200, 900);

            auto *content = new QWidget();
            auto *layout = new QVBoxLayout(content);

            auto *metaBox = new QGroupBox("Metadata");
            auto *metaForm = new QFormLayout(metaBox);
            metaForm->addRow("Name", new QLabel(metadata["name"].toString()));
            metaForm->addRow("Created", new QLabel(formatCreated(metadata["creationTimestamp"].toString())));
            metaForm->addRow("Age", new QLabel(computeAge(metadata["creationTimestamp"].toString())));
            metaForm->addRow("UID", new QLabel(metadata["uid"].toString()));
            auto *labelsValue = new QLabel(joinKeyValues(metadata["labels"].toObject()));
            labelsValue->setWordWrap(true);
            metaForm->addRow("Labels", labelsValue);
            auto *annotationsValue = new QLabel(joinKeyValues(metadata["annotations"].toObject()));
            annotationsValue->setWordWrap(true);
            metaForm->addRow("Annotations", annotationsValue);
            layout->addWidget(metaBox);

            auto *resourceBox = new QGroupBox("Resource information");
            auto *resourceForm = new QFormLayout(resourceBox);
            resourceForm->addRow("Provider ID", new QLabel(spec["providerID"].toString()));
            QStringList addresses;
            for (const auto &addressValue: status["addresses"].toArray()) {
                const QJsonObject address = addressValue.toObject();
                addresses << address["type"].toString() + ": " + address["address"].toString();
            }
            auto *addressesLabel = new QLabel(addresses.isEmpty() ? "-" : addresses.join("\n"));
            addressesLabel->setWordWrap(true);
            resourceForm->addRow("Addresses", addressesLabel);
            layout->addWidget(resourceBox);

            auto *systemBox = new QGroupBox("System information");
            auto *systemForm = new QFormLayout(systemBox);
            systemForm->addRow("Machine ID", new QLabel(nodeInfo["machineID"].toString()));
            systemForm->addRow("System UUID", new QLabel(nodeInfo["systemUUID"].toString()));
            systemForm->addRow("Boot ID", new QLabel(nodeInfo["bootID"].toString()));
            systemForm->addRow("Kernel Version", new QLabel(nodeInfo["kernelVersion"].toString()));
            systemForm->addRow("OS Image", new QLabel(nodeInfo["osImage"].toString()));
            systemForm->addRow("Container Runtime Version", new QLabel(nodeInfo["containerRuntimeVersion"].toString()));
            systemForm->addRow("Kubelet Version", new QLabel(nodeInfo["kubeletVersion"].toString()));
            systemForm->addRow("Operating System", new QLabel(nodeInfo["operatingSystem"].toString()));
            systemForm->addRow("Architecture", new QLabel(nodeInfo["architecture"].toString()));
            systemForm->addRow("CPU Capacity", new QLabel(capacity["cpu"].toString()));
            systemForm->addRow("Memory Capacity", new QLabel(formatMemoryGiB(parseMemoryBytes(capacity["memory"].toString()))));
            systemForm->addRow("Pods Capacity", new QLabel(capacity["pods"].toString()));
            layout->addWidget(systemBox);

            qint64 cpuRequestMillis = 0, cpuLimitMillis = 0;
            qint64 memRequestBytes = 0, memLimitBytes = 0;
            for (const auto &podValue: pods) {
                for (const auto &containerValue: podValue.toObject()["spec"].toObject()["containers"].toArray()) {
                    const QJsonObject resources = containerValue.toObject()["resources"].toObject();
                    cpuRequestMillis += parseCpuMillis(resources["requests"].toObject()["cpu"].toString());
                    cpuLimitMillis += parseCpuMillis(resources["limits"].toObject()["cpu"].toString());
                    memRequestBytes += parseMemoryBytes(resources["requests"].toObject()["memory"].toString());
                    memLimitBytes += parseMemoryBytes(resources["limits"].toObject()["memory"].toString());
                }
            }
            const qint64 cpuCapacityMillis = parseCpuMillis(allocatable["cpu"].toString());
            const qint64 memCapacityBytes = parseMemoryBytes(allocatable["memory"].toString());
            const int podsCapacity = allocatable["pods"].toString().toInt();

            const double cpuRequestPct = cpuCapacityMillis > 0 ? 100.0 * static_cast<double>(cpuRequestMillis) / static_cast<double>(cpuCapacityMillis) : 0.0;
            const double cpuLimitPct = cpuCapacityMillis > 0 ? 100.0 * static_cast<double>(cpuLimitMillis) / static_cast<double>(cpuCapacityMillis) : 0.0;
            const double memRequestPct = memCapacityBytes > 0 ? 100.0 * static_cast<double>(memRequestBytes) / static_cast<double>(memCapacityBytes) : 0.0;
            const double memLimitPct = memCapacityBytes > 0 ? 100.0 * static_cast<double>(memLimitBytes) / static_cast<double>(memCapacityBytes) : 0.0;
            const double podsPct = podsCapacity > 0 ? 100.0 * pods.size() / podsCapacity : 0.0;

            auto *allocationBox = new QGroupBox("Allocation");
            auto *allocationLayout = new QHBoxLayout(allocationBox);
            allocationLayout->addStretch();
            allocationLayout->addWidget(makeAllocationColumn("CPU", {
                new PercentageRing(cpuRequestPct, "Requests", QString("Cores: %1").arg(cpuRequestMillis / 1000.0, 0, 'f', 2)),
                new PercentageRing(cpuLimitPct, "Limits", QString("Cores: %1").arg(cpuLimitMillis / 1000.0, 0, 'f', 2)),
            }));
            allocationLayout->addStretch();
            allocationLayout->addWidget(makeAllocationColumn("Memory", {
                new PercentageRing(memRequestPct, "Requests", QString("GiB: %1").arg(memRequestBytes / (1024.0 * 1024 * 1024), 0, 'f', 1)),
                new PercentageRing(memLimitPct, "Limits", QString("GiB: %1").arg(memLimitBytes / (1024.0 * 1024 * 1024), 0, 'f', 1)),
            }));
            allocationLayout->addStretch();
            allocationLayout->addWidget(makeAllocationColumn("Pods", {
                new PercentageRing(podsPct, "Allocation", QString("Pods: %1").arg(pods.size())),
            }));
            allocationLayout->addStretch();
            layout->addWidget(allocationBox);

            auto *conditionsBox = new QGroupBox("Conditions");
            auto *conditionsLayout = new QVBoxLayout(conditionsBox);
            auto *conditionsTable = makeDetailTable({"Type", "Status", "Last Probe", "Last Transition", "Reason", "Message"});
            const QJsonArray conditions = status["conditions"].toArray();
            conditionsTable->setRowCount(conditions.size());
            for (int i = 0; i < conditions.size(); ++i) {
                const QJsonObject condition = conditions[i].toObject();
                conditionsTable->setItem(i, 0, new QTableWidgetItem(condition["type"].toString()));
                conditionsTable->setItem(i, 1, new QTableWidgetItem(condition["status"].toString()));
                conditionsTable->setItem(i, 2, new QTableWidgetItem(computeAge(condition["lastHeartbeatTime"].toString())));
                conditionsTable->setItem(i, 3, new QTableWidgetItem(computeAge(condition["lastTransitionTime"].toString())));
                conditionsTable->setItem(i, 4, new QTableWidgetItem(condition["reason"].toString()));
                conditionsTable->setItem(i, 5, new QTableWidgetItem(condition["message"].toString()));
            }
            conditionsLayout->addWidget(conditionsTable);
            layout->addWidget(conditionsBox);

            auto *podsBox = new QGroupBox(QString("Pods (%1)").arg(pods.size()));
            auto *podsLayout = new QVBoxLayout(podsBox);
            if (pods.isEmpty()) {
                auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
                emptyLabel->setAlignment(Qt::AlignCenter);
                podsLayout->addWidget(emptyLabel);
            } else {
                auto *podsTable = makeDetailTable({"Name", "Images", "Labels", "Node", "Status", "Restarts", "CPU Usage (cores)",
                                                    "Memory Usage (bytes)", "Created"});
                podsTable->setRowCount(pods.size());
                for (int i = 0; i < pods.size(); ++i) {
                    const QJsonObject pod = pods[i].toObject();
                    const QJsonObject podMeta = pod["metadata"].toObject();
                    const QJsonObject podStatus = pod["status"].toObject();
                    const QJsonObject podSpec = pod["spec"].toObject();

                    QStringList images;
                    for (const auto &containerValue: podSpec["containers"].toArray()) {
                        images << containerValue.toObject()["image"].toString();
                    }
                    int restarts = 0;
                    for (const auto &containerStatusValue: podStatus["containerStatuses"].toArray()) {
                        restarts += containerStatusValue.toObject()["restartCount"].toInt();
                    }

                    podsTable->setItem(i, 0, new QTableWidgetItem(podMeta["name"].toString()));
                    podsTable->setItem(i, 1, new QTableWidgetItem(images.join(", ")));
                    podsTable->setItem(i, 2, new QTableWidgetItem(joinKeyValues(podMeta["labels"].toObject())));
                    podsTable->setItem(i, 3, new QTableWidgetItem(podSpec["nodeName"].toString()));
                    podsTable->setItem(i, 4, new QTableWidgetItem(podStatus["phase"].toString()));
                    podsTable->setItem(i, 5, new QTableWidgetItem(QString::number(restarts)));
                    podsTable->setItem(i, 6, new QTableWidgetItem("-"));
                    podsTable->setItem(i, 7, new QTableWidgetItem("-"));
                    podsTable->setItem(i, 8, new QTableWidgetItem(computeAge(podMeta["creationTimestamp"].toString())));
                }
                podsLayout->addWidget(podsTable);
            }
            layout->addWidget(podsBox);

            auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
            auto *eventsLayout = new QVBoxLayout(eventsBox);
            auto *eventsTable = makeDetailTable({"Type", "Reason", "Message", "Age"});
            eventsTable->setRowCount(events.size());
            for (int i = 0; i < events.size(); ++i) {
                const QJsonObject event = events[i].toObject();
                eventsTable->setItem(i, 0, new QTableWidgetItem(event["type"].toString()));
                eventsTable->setItem(i, 1, new QTableWidgetItem(event["reason"].toString()));
                eventsTable->setItem(i, 2, new QTableWidgetItem(event["message"].toString()));
                eventsTable->setItem(i, 3, new QTableWidgetItem(computeAge(event["lastTimestamp"].toString())));
            }
            eventsLayout->addWidget(eventsTable);
            layout->addWidget(eventsBox);

            auto *scrollArea = new QScrollArea(&dialog);
            scrollArea->setWidgetResizable(true);
            scrollArea->setWidget(content);

            auto *dialogLayout = new QVBoxLayout(&dialog);
            dialogLayout->addWidget(scrollArea);

            dialog.exec();
        }

        // Shows a small dialog asking only for the authenticator token, and returns what
        // was typed (or an empty string if cancelled). Called by LoginWithOneLogin only
        // once the MFA challenge has actually arrived, so the code is submitted almost
        // immediately after being entered rather than sitting around expiring.
        QString promptForOtpToken() {
            QDialog dialog(this);
            dialog.setWindowTitle("OneLogin Sign In");

            auto *form = new QFormLayout(&dialog);
            auto *otpEdit = new QLineEdit(&dialog);
            form->addRow("Authenticator Token", otpEdit);

            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            buttons->button(QDialogButtonBox::Ok)->setText("Verify");
            form->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted) return {};
            return otpEdit->text();
        }

        // Writes the new session credentials to ~/.aws/credentials under a profile named
        // after the account, and points that kubeconfig context's exec plugin at that
        // profile -- so kubectl/aws used outside kubewatch (a plain terminal, another
        // tool) also picks up the freshly-obtained session. Best-effort: a failure here
        // doesn't affect kubewatch's own kubectl calls, which already use the in-memory
        // credentials directly.
        void updateExternalAwsConfig(const QString &accountKey, const AwsSessionCredentials &credentials, const QString &contextName) const {
            const QList<QStringList> setCalls = {
                {"configure", "set", "aws_access_key_id", credentials.accessKeyId, "--profile", accountKey},
                {"configure", "set", "aws_secret_access_key", credentials.secretAccessKey, "--profile", accountKey},
                {"configure", "set", "aws_session_token", credentials.sessionToken, "--profile", accountKey},
            };
            for (const QStringList &args: setCalls) {
                QProcess process;
                process.start("aws", args);
                if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                    logWarning << "Failed to update ~/.aws/credentials profile" << accountKey << ":"
                               << QString::fromLocal8Bit(process.readAllStandardError());
                    return;
                }
            }
            logInfo << "Updated ~/.aws/credentials profile" << accountKey;

            const KubectlResult viewResult = runKubectlCommand({"--kubeconfig", kKubeconfig, "config", "view", "-o", "json"});
            if (!viewResult.success) {
                logWarning << "Failed to read kubeconfig to update exec credentials:" << viewResult.error;
                return;
            }
            const QJsonArray contexts = QJsonDocument::fromJson(viewResult.output.toUtf8()).object()["contexts"].toArray();
            QString userName;
            for (const auto &contextValue: contexts) {
                if (const QJsonObject contextObj = contextValue.toObject(); contextObj["name"].toString() == contextName) {
                    userName = contextObj["context"].toObject()["user"].toString();
                    break;
                }
            }
            if (userName.isEmpty()) {
                logWarning << "Could not find kubeconfig user for context" << contextName;
                return;
            }

            const KubectlResult setCredsResult = runKubectlCommand(
                {"--kubeconfig", kKubeconfig, "config", "set-credentials", userName, "--exec-env=AWS_PROFILE=" + accountKey});
            if (!setCredsResult.success) {
                logWarning << "Failed to update kubeconfig exec env for user" << userName << ":" << setCredsResult.error;
                return;
            }
            logInfo << "Updated kubeconfig user" << userName << "to use AWS profile" << accountKey;
        }

        // Returns true if login completed successfully (in which case credentials,
        // namespaces and the current page have already been refreshed), false if the
        // dialog was cancelled or login failed for any reason.
        bool showLoginDialog() {
            QDialog dialog(this);
            dialog.setWindowTitle("OneLogin Sign In");
            dialog.setMinimumWidth(450);

            auto *form = new QFormLayout(&dialog);

            auto *contextCombo = new QComboBox(&dialog);
            for (int i = 0; i < contextBox_->count(); ++i) {
                contextCombo->addItem(contextBox_->itemText(i));
            }
            contextCombo->setCurrentText(contextBox_->currentText());
            form->addRow("Context", contextCombo);

            auto *usernameEdit = new QLineEdit(&dialog);
            usernameEdit->setText(Configuration::instance().GetValue<QString>("onelogin.user", QString()));
            auto *passwordEdit = new QLineEdit(&dialog);
            passwordEdit->setEchoMode(QLineEdit::Password);

            form->addRow("Username:", usernameEdit);
            form->addRow("Password:", passwordEdit);

            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            buttons->button(QDialogButtonBox::Ok)->setText("Login");
            form->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            if (dialog.exec() != QDialog::Accepted) return false;

            const QString selectedContext = contextCombo->currentText();
            if (selectedContext.isEmpty()) {
                QMessageBox::warning(this, "Login", "No context selected.");
                return false;
            }
            // Switch the main window to the chosen context too, so the user ends up
            // browsing whatever cluster they just signed in for.
            if (const int idx = contextBox_->findText(selectedContext); idx >= 0 && idx != contextBox_->currentIndex()) {
                contextBox_->setCurrentIndex(idx);
            }

            const QString username = usernameEdit->text();
            const QString password = passwordEdit->text();
            if (username.isEmpty() || password.isEmpty()) {
                QMessageBox::warning(this, "Login", "Username and password are both required.");
                return false;
            }

            const QString accountKey = currentAwsAccountKey();
            const long appId = Configuration::instance().GetValue<long>("onelogin.app-id." + accountKey, 0L);
            if (appId == 0) {
                QMessageBox::warning(this, "Login",
                                      "No OneLogin app-id configured for account \"" + accountKey + "\".");
                return false;
            }

            AwsSessionCredentials credentials;
            QString error;
            {
                BusyGuard busyGuard;
                credentials = LoginWithOneLogin(
                    username, password, [this] { return promptForOtpToken(); }, appId, &error);
            }

            if (!credentials.isValid()) {
                QMessageBox::warning(this, "Login failed", error.isEmpty() ? "Unknown error." : error);
                return false;
            }

            awsCredentialsByAccount_[accountKey] = credentials;
            updateActiveAwsCredentials();
            loadNamespaces();
            refreshCurrentPage();
            Configuration::instance().SetValue("onelogin.user", username);
            updateExternalAwsConfig(accountKey, credentials, selectedContext);

            QMessageBox::information(this, "Login",
                                      "Signed in for account \"" + accountKey + "\". Session valid until " +
                                          credentials.expiresAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") + ".");
            return true;
        }

        QComboBox *contextBox_;
        QComboBox *namespaceBox_;
        QLabel *statusLabel_;
        QTreeWidget *tree_;
        QStackedWidget *pages_;
        QListWidget *logWidget_;
        QToolButton *autoScrollLogsButton_;
        QTreeWidgetItem *nodesItem_ = nullptr;
        std::vector<ResourceSpec> specs_;
        QMap<QString, AwsSessionCredentials> awsCredentialsByAccount_;
        UpdateChecker *updateChecker_ = nullptr;
    };
}

namespace {
    void applyDarkTheme(QApplication &app) {
        QApplication::setStyle("Fusion");

        QPalette palette;
        palette.setColor(QPalette::Window, QColor(53, 53, 53));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(35, 35, 35));
        palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(53, 53, 53));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::black);
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));

        QApplication::setPalette(palette);
    }
} // namespace

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setWindowIcon(IconUtils::GetCommonIcon("kubewatch"));
    applyDarkTheme(a);
    Configuration::instance().SetFilePath(ensureConfigFile());
    qInstallMessageHandler(myCustomMessageHandler);
    LogSignaler::instance().SetLevel(DEBUG);
    MainWindow window;
    window.show();
    return QApplication::exec();
}
