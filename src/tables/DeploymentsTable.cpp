#include <tables/DeploymentsTable.h>

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

    Health ComputeHealth(const QJsonObject &deployment) {
        const int ready = deployment["status"].toObject()["readyReplicas"].toInt();
        const int desired = deployment["spec"].toObject()["replicas"].toInt(1);
        if (desired > 0 && ready == 0) return Health::Error;
        if (ready < desired) return Health::Warning;
        return Health::Ok;
    }
}// namespace

DeploymentsTable::DeploymentsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Pods", "Age", "", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    // Keep "Name" as logical column 0 (row selection and context-menu actions rely
    // on it), but display Health as the leftmost column.
    MoveColumnToFront(kHealthColumn);
    SetResizeModes({QHeaderView::Stretch, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents});
    setServiceApis({"deployments"});
}

void DeploymentsTable::Refresh() {
    QElapsedTimer timer;
    timer.start();
    const QJsonArray items = KubectlClient::fetchItems(ResourceArgs("deployments"));

    PopulatePage(items, kHealthColumn, [](const QJsonObject &deployment) { return static_cast<long>(ComputeHealth(deployment)); }, [&](const int row, const QJsonObject &deployment) {
        const QJsonObject metadata = deployment["metadata"].toObject();
        const QJsonObject status = deployment["status"].toObject();
        const QJsonObject spec = deployment["spec"].toObject();

        const int ready = status["readyReplicas"].toInt();
        const int desired = spec["replicas"].toInt(1);
        const QString pods = QString("%1/%2").arg(ready).arg(desired);

        const Health health = ComputeHealth(deployment);

        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, pods, Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, 2, KubeFormat::computeAge(metadata["creationTimestamp"].toString()));
        SetColumn(row, kHealthColumn, HealthLight::Icon(health), static_cast<long>(health), HealthTooltip(health));
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("deployments", timer.elapsed());
}
