#include <tables/PodsTable.h>

#include <kubectl/KubectlClient.h>
#include <utils/EventBus.h>
#include <utils/KubeFormat.h>

#include <QElapsedTimer>

PodsTable::PodsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Ready", "Status", "Restarts", "Age", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
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

        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, ready);
        SetColumn(row, 2, podStatus);
        SetColumn(row, 3, static_cast<long>(restarts));
        SetColumn(row, 4, KubeFormat::computeAge(metadata["creationTimestamp"].toString()));
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("pods", timer.elapsed());
}
