#include <tables/PodsTable.h>

#include <kubectl/KubeNetService.h>
#include <utils/EventBus.h>
#include <utils/HealthLight.h>
#include <utils/KubeFormat.h>
#include <utils/Logging.h>

#include <QElapsedTimer>

using HealthLight::Health;

namespace {
    QString HealthTooltip(const Health health, const QString &podStatus) {
        switch (health) {
            case Health::Error:
                return "Pod is not running";
            case Health::Warning:
                return "Not all containers are ready";
            default:
                return podStatus == "Succeeded" ? "Completed successfully" : "Running and ready";
        }
    }

    // Recomputes just enough of the pod's status to derive its health -- mirrors the
    // fuller parse in Refresh()'s row callback, which also needs podStatus/ready/restarts
    // for display.
    Health ComputeHealth(const QJsonObject &pod) {
        const QJsonObject metadata = pod["metadata"].toObject();
        const QJsonObject status = pod["status"].toObject();

        QString podStatus = status["phase"].toString();
        int readyContainers = 0;
        const QJsonArray containerStatuses = status["containerStatuses"].toArray();
        for (const auto &containerStatusValue: containerStatuses) {
            const QJsonObject containerStatus = containerStatusValue.toObject();
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

        if (podStatus == "Succeeded") {
            // Job/CronJob pods exit after completion, so 0 ready containers is expected.
            return Health::Ok;
        }
        if (podStatus != "Running") return Health::Error;
        if (readyContainers != containerStatuses.size()) return Health::Warning;
        return Health::Ok;
    }
}// namespace

PodsTable::PodsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Ready", "Status", "Restarts", "Age", "", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    // Keep "Name" as logical column 0 (row selection and context-menu actions rely
    // on it), but display Health as the leftmost column.
    MoveColumnToFront(kHealthColumn);
    SetResizeModes({QHeaderView::Stretch, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents});
    setServiceApis({"pods"});
}

void PodsTable::Refresh() {
    QElapsedTimer timer;
    timer.start();

    KubeNetService &svc = KubeNetService::forContext(CurrentContext());
    if (!svc.IsValid()) {
        logWarning << svc.LastError();
        return;
    }
    const QJsonArray items = svc.fetchItems(KubeNetService::ResourcePath("pods", CurrentNamespace()));

    PopulatePage(items, kHealthColumn, [](const QJsonObject &pod) { return static_cast<long>(ComputeHealth(pod)); }, [&](const int row, const QJsonObject &pod) {
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

        const Health health = ComputeHealth(pod);

        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, ready);
        SetColumn(row, 2, podStatus);
        SetColumn(row, 3, static_cast<long>(restarts));
        SetColumn(row, 4, KubeFormat::computeAge(metadata["creationTimestamp"].toString()));
        SetColumn(row, kHealthColumn, HealthLight::Icon(health), static_cast<long>(health), HealthTooltip(health, podStatus));
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("pods", timer.elapsed());
}
