#include <tables/DaemonSetsTable.h>

#include <kubectl/KubeNetService.h>
#include <utils/EventBus.h>
#include <utils/KubeFormat.h>
#include <utils/Logging.h>

#include <QElapsedTimer>

DaemonSetsTable::DaemonSetsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Age", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    setServiceApis({"daemonsets"});
}

void DaemonSetsTable::Refresh() {
    QElapsedTimer timer;
    timer.start();

    KubeNetService &svc = KubeNetService::forContext(CurrentContext());
    if (!svc.IsValid()) {
        logWarning << svc.LastError();
        return;
    }
    const QJsonArray items = svc.fetchItems(KubeNetService::ResourcePath("daemonsets", CurrentNamespace()));

    PopulatePage(items, [&](const int row, const QJsonObject &obj) {
        const QJsonObject metadata = obj["metadata"].toObject();
        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, KubeFormat::computeAge(metadata["creationTimestamp"].toString()));
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("daemonsets", timer.elapsed());
}
