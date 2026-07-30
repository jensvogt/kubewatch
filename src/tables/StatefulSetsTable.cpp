#include <tables/StatefulSetsTable.h>

#include <kubectl/KubeNetService.h>
#include <utils/EventBus.h>
#include <utils/KubeFormat.h>
#include <utils/Logging.h>

#include <QElapsedTimer>

StatefulSetsTable::StatefulSetsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Age", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    setServiceApis({"statefulsets"});
}

void StatefulSetsTable::Refresh() {
    QElapsedTimer timer;
    timer.start();

    KubeNetService &svc = KubeNetService::forContext(CurrentContext());
    if (!svc.IsValid()) {
        logWarning << svc.LastError();
        return;
    }
    const QJsonArray items = svc.fetchItems(KubeNetService::ResourcePath("statefulsets", CurrentNamespace()));

    PopulatePage(items, [&](const int row, const QJsonObject &obj) {
        const QJsonObject metadata = obj["metadata"].toObject();
        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, KubeFormat::computeAge(metadata["creationTimestamp"].toString()));
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("statefulsets", timer.elapsed());
}
