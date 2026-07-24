#include <dialogs/NodeDetailsDialog.h>

#include <kubectl/KubectlClient.h>
#include <utils/KubeFormat.h>

#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
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
} // namespace

void NodeDetailsDialog::Show(QWidget *parent, const QStringList &baseArgs, const QString &name) {
    QJsonObject node;
    QJsonArray pods;
    QJsonArray events;
    {
        BusyGuard busyGuard;
        QStringList getArgs = baseArgs;
        getArgs << "get" << "nodes" << name << "-o" << "json";
        const KubectlResult nodeResult = KubectlClient::runKubectlCommand(getArgs);
        if (!nodeResult.success) {
            QMessageBox::warning(parent, "Node details failed", nodeResult.error);
            return;
        }
        node = QJsonDocument::fromJson(nodeResult.output.toUtf8()).object();

        QStringList podArgs = baseArgs;
        podArgs << "get" << "pods" << "--all-namespaces" << "--field-selector" << "spec.nodeName=" + name << "-o" << "json";
        // Excludes Succeeded/Failed pods (e.g. completed Job pods still lingering
        // on the node) to match kubectl describe node / the Kubernetes Dashboard.
        for (const auto &podValue: KubectlClient::fetchItems(podArgs)) {
            if (!KubeFormat::isTerminatedPodPhase(podValue.toObject()["status"].toObject()["phase"].toString())) {
                pods.append(podValue);
            }
        }

        QStringList eventArgs = baseArgs;
        eventArgs << "get" << "events" << "--all-namespaces" << "--field-selector"
                  << "involvedObject.name=" + name + ",involvedObject.kind=Node" << "-o" << "json";
        events = KubectlClient::fetchItems(eventArgs);
    }

    const QJsonObject metadata = node["metadata"].toObject();
    const QJsonObject spec = node["spec"].toObject();
    const QJsonObject status = node["status"].toObject();
    const QJsonObject nodeInfo = status["nodeInfo"].toObject();
    const QJsonObject capacity = status["capacity"].toObject();
    const QJsonObject allocatable = status["allocatable"].toObject();

    QDialog dialog(parent);
    dialog.setWindowTitle("Node: " + name);
    dialog.resize(1200, 900);

    auto *content = new QWidget();
    auto *layout = new QVBoxLayout(content);

    auto *metaBox = new QGroupBox("Metadata");
    auto *metaForm = new QFormLayout(metaBox);
    metaForm->addRow("Name", new QLabel(metadata["name"].toString()));
    metaForm->addRow("Created", new QLabel(KubeFormat::formatCreated(metadata["creationTimestamp"].toString())));
    metaForm->addRow("Age", new QLabel(KubeFormat::computeAge(metadata["creationTimestamp"].toString())));
    metaForm->addRow("UID", new QLabel(metadata["uid"].toString()));
    auto *labelsValue = new QLabel(KubeFormat::joinKeyValues(metadata["labels"].toObject()));
    labelsValue->setWordWrap(true);
    metaForm->addRow("Labels", labelsValue);
    auto *annotationsValue = new QLabel(KubeFormat::joinKeyValues(metadata["annotations"].toObject()));
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
    systemForm->addRow("Memory Capacity", new QLabel(KubeFormat::formatMemoryGiB(KubeFormat::parseMemoryBytes(capacity["memory"].toString()))));
    systemForm->addRow("Pods Capacity", new QLabel(capacity["pods"].toString()));
    layout->addWidget(systemBox);

    qint64 cpuRequestMillis = 0, cpuLimitMillis = 0;
    qint64 memRequestBytes = 0, memLimitBytes = 0;
    for (const auto &podValue: pods) {
        for (const auto &containerValue: podValue.toObject()["spec"].toObject()["containers"].toArray()) {
            const QJsonObject resources = containerValue.toObject()["resources"].toObject();
            cpuRequestMillis += KubeFormat::parseCpuMillis(resources["requests"].toObject()["cpu"].toString());
            cpuLimitMillis += KubeFormat::parseCpuMillis(resources["limits"].toObject()["cpu"].toString());
            memRequestBytes += KubeFormat::parseMemoryBytes(resources["requests"].toObject()["memory"].toString());
            memLimitBytes += KubeFormat::parseMemoryBytes(resources["limits"].toObject()["memory"].toString());
        }
    }
    const qint64 cpuCapacityMillis = KubeFormat::parseCpuMillis(allocatable["cpu"].toString());
    const qint64 memCapacityBytes = KubeFormat::parseMemoryBytes(allocatable["memory"].toString());
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
    auto *conditionsTable = KubeFormat::makeDetailTable({"Type", "Status", "Last Probe", "Last Transition", "Reason", "Message"});
    const QJsonArray conditions = status["conditions"].toArray();
    conditionsTable->setRowCount(conditions.size());
    for (int i = 0; i < conditions.size(); ++i) {
        const QJsonObject condition = conditions[i].toObject();
        conditionsTable->setItem(i, 0, new QTableWidgetItem(condition["type"].toString()));
        conditionsTable->setItem(i, 1, new QTableWidgetItem(condition["status"].toString()));
        conditionsTable->setItem(i, 2, new QTableWidgetItem(KubeFormat::computeAge(condition["lastHeartbeatTime"].toString())));
        conditionsTable->setItem(i, 3, new QTableWidgetItem(KubeFormat::computeAge(condition["lastTransitionTime"].toString())));
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
        auto *podsTable = KubeFormat::makeDetailTable({"Name", "Images", "Labels", "Node", "Status", "Restarts", "CPU Usage (cores)",
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
            podsTable->setItem(i, 2, new QTableWidgetItem(KubeFormat::joinKeyValues(podMeta["labels"].toObject())));
            podsTable->setItem(i, 3, new QTableWidgetItem(podSpec["nodeName"].toString()));
            podsTable->setItem(i, 4, new QTableWidgetItem(podStatus["phase"].toString()));
            podsTable->setItem(i, 5, new QTableWidgetItem(QString::number(restarts)));
            podsTable->setItem(i, 6, new QTableWidgetItem("-"));
            podsTable->setItem(i, 7, new QTableWidgetItem("-"));
            podsTable->setItem(i, 8, new QTableWidgetItem(KubeFormat::computeAge(podMeta["creationTimestamp"].toString())));
        }
        podsLayout->addWidget(podsTable);
    }
    layout->addWidget(podsBox);

    auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
    auto *eventsLayout = new QVBoxLayout(eventsBox);
    auto *eventsTable = KubeFormat::makeDetailTable({"Type", "Reason", "Message", "Age"});
    eventsTable->setRowCount(events.size());
    for (int i = 0; i < events.size(); ++i) {
        const QJsonObject event = events[i].toObject();
        eventsTable->setItem(i, 0, new QTableWidgetItem(event["type"].toString()));
        eventsTable->setItem(i, 1, new QTableWidgetItem(event["reason"].toString()));
        eventsTable->setItem(i, 2, new QTableWidgetItem(event["message"].toString()));
        eventsTable->setItem(i, 3, new QTableWidgetItem(KubeFormat::computeAge(event["lastTimestamp"].toString())));
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
