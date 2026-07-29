#include <tables/ReplicaSetsTable.h>

#include <kubectl/KubectlClient.h>
#include <utils/EventBus.h>
#include <utils/HealthLight.h>
#include <utils/KubeFormat.h>

#include <QElapsedTimer>

using HealthLight::Health;

namespace {
    QString HealthTooltip(const Health health) {
        switch (health) {
            case Health::Error:
                return "No pods are ready";
            case Health::Warning:
                return "Not all pods are ready";
            default:
                return "All pods are ready";
        }
    }

    Health ComputeHealth(const QJsonObject &replicaSet) {
        const int ready = replicaSet["status"].toObject()["readyReplicas"].toInt();
        const int desired = replicaSet["spec"].toObject()["replicas"].toInt(1);
        if (desired > 0 && ready == 0) return Health::Error;
        if (ready < desired) return Health::Warning;
        return Health::Ok;
    }
}// namespace

ReplicaSetsTable::ReplicaSetsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Pods", "Age", "", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    // Keep "Name" as logical column 0 (row selection and context-menu actions rely
    // on it), but display Health as the leftmost column.
    MoveColumnToFront(kHealthColumn);
    SetResizeModes({QHeaderView::Stretch, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents});
    setServiceApis({"replicasets"});
}

void ReplicaSetsTable::Refresh() {
    QElapsedTimer timer;
    timer.start();
    const QJsonArray items = KubectlClient::fetchItems(ResourceArgs("replicasets"));

    PopulatePage(items, kHealthColumn, [](const QJsonObject &replicaSet) { return static_cast<long>(ComputeHealth(replicaSet)); }, [&](const int row, const QJsonObject &replicaSet) {
        const QJsonObject metadata = replicaSet["metadata"].toObject();
        const QJsonObject status = replicaSet["status"].toObject();
        const QJsonObject spec = replicaSet["spec"].toObject();

        const int ready = status["readyReplicas"].toInt();
        const int desired = spec["replicas"].toInt(1);
        const QString pods = QString("%1/%2").arg(ready).arg(desired);

        const Health health = ComputeHealth(replicaSet);

        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, pods, Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, 2, KubeFormat::computeAge(metadata["creationTimestamp"].toString()));
        SetColumn(row, kHealthColumn, HealthLight::Icon(health), static_cast<long>(health), HealthTooltip(health));
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("replicasets", timer.elapsed());
}
