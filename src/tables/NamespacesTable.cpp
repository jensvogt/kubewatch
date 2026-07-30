#include <tables/NamespacesTable.h>

#include <kubectl/KubeNetService.h>
#include <utils/EventBus.h>
#include <utils/KubeFormat.h>
#include <utils/Logging.h>

#include <QElapsedTimer>

NamespacesTable::NamespacesTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Status", "Age"});
    setServiceApis({"namespaces"});
}

void NamespacesTable::Refresh() {
    QElapsedTimer timer;
    timer.start();

    KubeNetService &svc = KubeNetService::forContext(CurrentContext());
    if (!svc.IsValid()) {
        logWarning << svc.LastError();
        return;
    }
    const QJsonArray items = svc.fetchItems(KubeNetService::ResourcePath("namespaces"));

    PopulatePage(items, [&](const int row, const QJsonObject &obj) {
        const QJsonObject metadata = obj["metadata"].toObject();
        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, obj["status"].toObject()["phase"].toString());
        SetColumn(row, 2, KubeFormat::computeAge(metadata["creationTimestamp"].toString()));
    });
    EventBus::instance().TimerSignal("namespaces", timer.elapsed());
}
