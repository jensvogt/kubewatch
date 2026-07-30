#include <dialogs/NamespaceDetailsDialog.h>

#include <kubectl/KubectlClient.h>
#include <kubectl/KubeNetService.h>
#include <utils/KubeFormat.h>

#include <QBrush>
#include <QColor>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
    QLabel *emptyStateLabel() {
        auto *label = new QLabel("There is nothing to display here\nNo resources found.");
        label->setAlignment(Qt::AlignCenter);
        return label;
    }

    QString valueOrDash(const QJsonObject &map, const QString &key) {
        return map.contains(key) ? map[key].toString() : "-";
    }
} // namespace

void NamespaceDetailsDialog::Show(QWidget *parent, const QString &context, const QString &name) {
    QJsonObject ns;
    QJsonArray resourceQuotas;
    QJsonArray limitRanges;
    QJsonArray events;
    {
        BusyGuard busyGuard;

        KubeNetService &svc = KubeNetService::forContext(context);
        if (!svc.IsValid()) {
            QMessageBox::warning(parent, "Namespace details failed", svc.LastError());
            return;
        }

        ns = svc.fetchObject(KubeNetService::ResourcePath("namespaces") + "/" + name);
        if (ns.isEmpty()) {
            QMessageBox::warning(parent, "Namespace details failed", svc.LastError());
            return;
        }

        resourceQuotas = svc.fetchItems(KubeNetService::ResourcePath("resourcequotas", name));
        limitRanges = svc.fetchItems(KubeNetService::ResourcePath("limitranges", name));
        events = svc.fetchItems(KubeNetService::ResourcePath("events", name));
    }

    const QJsonObject metadata = ns["metadata"].toObject();

    QDialog dialog(parent);
    dialog.setWindowTitle("Namespace: " + name);
    dialog.resize(1200, 900);

    auto *content = new QWidget();
    auto *layout = new QVBoxLayout(content);

    auto *metaBox = new QGroupBox("Metadata");
    auto *metaForm = new QFormLayout(metaBox);
    metaForm->addRow("Name", new QLabel(metadata["name"].toString()));
    metaForm->addRow("Created", new QLabel(KubeFormat::formatCreated(metadata["creationTimestamp"].toString())));
    metaForm->addRow("Age", new QLabel(KubeFormat::computeAge(metadata["creationTimestamp"].toString())));
    metaForm->addRow("UID", new QLabel(metadata["uid"].toString()));
    auto *labelsValue = new QLabel(KubeFormat::joinKeyValues(metadata["labels"].toObject()));
    labelsValue->setWordWrap(true);
    metaForm->addRow("Labels", labelsValue);
    auto *annotationsValue = new QLabel(KubeFormat::joinKeyValues(metadata["annotations"].toObject()));
    annotationsValue->setWordWrap(true);
    metaForm->addRow("Annotations", annotationsValue);
    layout->addWidget(metaBox);

    auto *resourceBox = new QGroupBox("Resource information");
    auto *resourceForm = new QFormLayout(resourceBox);
    resourceForm->addRow("Status", new QLabel(ns["status"].toObject()["phase"].toString()));
    layout->addWidget(resourceBox);

    auto *quotasBox = new QGroupBox("Resource Quotas");
    auto *quotasLayout = new QVBoxLayout(quotasBox);
    if (resourceQuotas.isEmpty()) {
        quotasLayout->addWidget(emptyStateLabel());
    } else {
        auto *quotasTable = KubeFormat::makeDetailTable({"Name", "Hard", "Used"});
        quotasTable->setRowCount(resourceQuotas.size());
        for (int i = 0; i < resourceQuotas.size(); ++i) {
            const QJsonObject quota = resourceQuotas[i].toObject();
            const QJsonObject quotaStatus = quota["status"].toObject();
            const QJsonObject hardMap =
                    quotaStatus.contains("hard") ? quotaStatus["hard"].toObject() : quota["spec"].toObject()["hard"].toObject();
            quotasTable->setItem(i, 0, new QTableWidgetItem(quota["metadata"].toObject()["name"].toString()));
            quotasTable->setItem(i, 1, new QTableWidgetItem(KubeFormat::formatResourceList(hardMap)));
            quotasTable->setItem(i, 2, new QTableWidgetItem(KubeFormat::formatResourceList(quotaStatus["used"].toObject())));
        }
        quotasLayout->addWidget(quotasTable);
    }
    layout->addWidget(quotasBox);

    auto *limitsBox = new QGroupBox("Resource Limits");
    auto *limitsLayout = new QVBoxLayout(limitsBox);
    struct LimitRow {
        QString type, resource, min, max, defaultValue, defaultRequest;
    };
    QList<LimitRow> limitRows;
    for (const auto &limitRangeValue: limitRanges) {
        for (const auto &limitValue: limitRangeValue.toObject()["spec"].toObject()["limits"].toArray()) {
            const QJsonObject limit = limitValue.toObject();
            const QJsonObject maxMap = limit["max"].toObject();
            const QJsonObject minMap = limit["min"].toObject();
            const QJsonObject defaultMap = limit["default"].toObject();
            const QJsonObject defaultRequestMap = limit["defaultRequest"].toObject();

            QSet<QString> resources;
            for (const QJsonObject &map: {maxMap, minMap, defaultMap, defaultRequestMap}) {
                for (auto it = map.begin(); it != map.end(); ++it) resources.insert(it.key());
            }
            QStringList sortedResources(resources.begin(), resources.end());
            std::sort(sortedResources.begin(), sortedResources.end());
            for (const QString &resource: sortedResources) {
                limitRows.append({limit["type"].toString(), resource, valueOrDash(minMap, resource), valueOrDash(maxMap, resource),
                                   valueOrDash(defaultMap, resource), valueOrDash(defaultRequestMap, resource)});
            }
        }
    }
    if (limitRows.isEmpty()) {
        limitsLayout->addWidget(emptyStateLabel());
    } else {
        auto *limitsTable = KubeFormat::makeDetailTable({"Type", "Resource", "Min", "Max", "Default", "Default Request"});
        limitsTable->setRowCount(limitRows.size());
        for (int i = 0; i < limitRows.size(); ++i) {
            const LimitRow &row = limitRows[i];
            limitsTable->setItem(i, 0, new QTableWidgetItem(row.type));
            limitsTable->setItem(i, 1, new QTableWidgetItem(row.resource));
            limitsTable->setItem(i, 2, new QTableWidgetItem(row.min));
            limitsTable->setItem(i, 3, new QTableWidgetItem(row.max));
            limitsTable->setItem(i, 4, new QTableWidgetItem(row.defaultValue));
            limitsTable->setItem(i, 5, new QTableWidgetItem(row.defaultRequest));
        }
        limitsLayout->addWidget(limitsTable);
    }
    layout->addWidget(limitsBox);

    auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
    auto *eventsLayout = new QVBoxLayout(eventsBox);
    auto *eventsTable =
            KubeFormat::makeDetailTable({"Name", "Reason", "Message", "Source", "Sub-object", "Count", "First Seen", "Last Seen"});
    eventsTable->setRowCount(events.size());
    for (int i = 0; i < events.size(); ++i) {
        const QJsonObject event = events[i].toObject();
        const QJsonObject source = event["source"].toObject();
        QStringList sourceParts;
        if (const QString component = source["component"].toString(); !component.isEmpty()) sourceParts << component;
        if (const QString host = source["host"].toString(); !host.isEmpty()) sourceParts << host;
        const QString subObject = event["involvedObject"].toObject()["fieldPath"].toString();

        auto *nameItem = new QTableWidgetItem(event["metadata"].toObject()["name"].toString());
        auto *reasonItem = new QTableWidgetItem(event["reason"].toString());
        if (event["type"].toString() == "Warning") {
            nameItem->setForeground(QBrush(QColor(0xFB, 0x8C, 0x00)));
            reasonItem->setForeground(QBrush(QColor(0xFB, 0x8C, 0x00)));
        }
        eventsTable->setItem(i, 0, nameItem);
        eventsTable->setItem(i, 1, reasonItem);
        eventsTable->setItem(i, 2, new QTableWidgetItem(event["message"].toString()));
        eventsTable->setItem(i, 3, new QTableWidgetItem(sourceParts.isEmpty() ? "-" : sourceParts.join(" ")));
        eventsTable->setItem(i, 4, new QTableWidgetItem(subObject.isEmpty() ? "-" : subObject));
        eventsTable->setItem(i, 5, new QTableWidgetItem(QString::number(event["count"].toInt(1))));
        eventsTable->setItem(i, 6, new QTableWidgetItem(KubeFormat::computeAge(event["firstTimestamp"].toString())));
        eventsTable->setItem(i, 7, new QTableWidgetItem(KubeFormat::computeAge(event["lastTimestamp"].toString())));
    }
    eventsLayout->addWidget(eventsTable);
    layout->addWidget(eventsBox);

    auto *scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(content);

    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addWidget(scrollArea);

    dialog.exec();
}
