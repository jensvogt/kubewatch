#include <tables/EventsTable.h>

#include <kubectl/KubectlClient.h>
#include <utils/EventBus.h>
#include <utils/HealthLight.h>
#include <utils/KubeFormat.h>

#include <QElapsedTimer>

using HealthLight::Health;

EventsTable::EventsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Type", "Reason", "Object", "Message", "Last Seen", "", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    // Keep "Name" as logical column 0 (row selection and context-menu actions rely
    // on it), but display Health as the leftmost column.
    MoveColumnToFront(kHealthColumn);
    SetResizeModes({QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::Stretch, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents});
    setServiceApis({"events"});
}

void EventsTable::Refresh() {
    QElapsedTimer timer;
    timer.start();
    const QJsonArray items = KubectlClient::fetchItems(ResourceArgs("events"));

    const auto healthOf = [](const QJsonObject &event) {
        return static_cast<long>(event["type"].toString() == "Warning" ? Health::Warning : Health::Ok);
    };

    PopulatePage(items, kHealthColumn, healthOf, [&](const int row, const QJsonObject &event) {
        const QJsonObject metadata = event["metadata"].toObject();
        const QJsonObject involvedObject = event["involvedObject"].toObject();

        const QString type = event["type"].toString();
        // Prefer lastTimestamp (classic events), fall back to eventTime (events.k8s.io),
        // then the event's own creation time if neither is set.
        QString lastSeen = event["lastTimestamp"].toString();
        if (lastSeen.isEmpty()) lastSeen = event["eventTime"].toString();
        if (lastSeen.isEmpty()) lastSeen = metadata["creationTimestamp"].toString();

        const QString object = involvedObject["kind"].toString() + "/" + involvedObject["name"].toString();
        const auto health = static_cast<Health>(healthOf(event));

        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, type);
        SetColumn(row, 2, event["reason"].toString());
        SetColumn(row, 3, object);
        SetColumn(row, 4, event["message"].toString());
        SetColumn(row, 5, KubeFormat::computeAge(lastSeen));
        SetColumn(row, kHealthColumn, HealthLight::Icon(health), static_cast<long>(health), type);
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("events", timer.elapsed());
}
