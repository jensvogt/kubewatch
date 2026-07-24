#include <tables/GenericTable.h>

#include <kubectl/KubectlClient.h>
#include <utils/EventBus.h>
#include <utils/KubeFormat.h>

#include <QElapsedTimer>

GenericTable::GenericTable(QString resource, QWidget *parent) : KubeTable(parent), resource_(std::move(resource)) {
    ConfigureHeaders({"Name", "Age", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    setServiceApis({resource_});
}

void GenericTable::Refresh() {
    QElapsedTimer timer;
    timer.start();
    const QJsonArray items = KubectlClient::fetchItems(ResourceArgs(resource_));

    PopulatePage(items, [&](int row, const QJsonObject &obj) {
        const QJsonObject metadata = obj["metadata"].toObject();
        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, KubeFormat::computeAge(metadata["creationTimestamp"].toString()));
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal(resource_, timer.elapsed());
}
