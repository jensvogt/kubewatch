#include <tables/PodsTable.h>

#include <kubectl/KubectlClient.h>
#include <utils/EventBus.h>
#include <utils/HealthLight.h>
#include <utils/KubeFormat.h>

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
}// namespace

PodsTable::PodsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Ready", "Status", "Restarts", "Age", "Health", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    // Keep "Name" as logical column 0 (row selection and context-menu actions rely
    // on it), but display Health as the leftmost column.
    MoveColumnToFront(kHealthColumn);
    setServiceApis({"pods"});
}

void PodsTable::Refresh() {
    QElapsedTimer timer;
    timer.start();
    const QJsonArray items = KubectlClient::fetchItems(ResourceArgs("pods"));

    PopulatePage(items, [&](const int row, const QJsonObject &pod) {
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

        Health health;
        if (podStatus == "Succeeded") {
            // Job/CronJob pods exit after completion, so 0 ready containers is expected.
            health = Health::Ok;
        } else if (podStatus != "Running") {
            health = Health::Error;
        } else if (readyContainers != containerStatuses.size()) {
            health = Health::Warning;
        } else {
            health = Health::Ok;
        }

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
