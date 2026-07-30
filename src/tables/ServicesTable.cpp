#include <tables/ServicesTable.h>

#include <kubectl/KubeNetService.h>
#include <utils/EventBus.h>
#include <utils/KubeFormat.h>
#include <utils/Logging.h>

#include <QElapsedTimer>

ServicesTable::ServicesTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Type", "Cluster IP", "Created", "Age", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    setServiceApis({"services"});
}

void ServicesTable::Refresh() {
    QElapsedTimer timer;
    timer.start();

    KubeNetService &svc = KubeNetService::forContext(CurrentContext());
    if (!svc.IsValid()) {
        logWarning << svc.LastError();
        return;
    }
    const QJsonArray items = svc.fetchItems(KubeNetService::ResourcePath("services", CurrentNamespace()));

    PopulatePage(items, [&](int row, const QJsonObject &svc) {
        const QJsonObject metadata = svc["metadata"].toObject();
        const QJsonObject spec = svc["spec"].toObject();

        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, spec["type"].toString());
        SetColumn(row, 2, spec["clusterIP"].toString());
        SetColumn(row, 3, KubeFormat::formatCreated(metadata["creationTimestamp"].toString()));
        SetColumn(row, 4, KubeFormat::computeAge(metadata["creationTimestamp"].toString()),
                  Qt::AlignRight | Qt::AlignVCenter);
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("services", timer.elapsed());
}
