#include <dialogs/ReplicaSetDetailsDialog.h>

#include <kubectl/KubectlClient.h>
#include <kubectl/KubeNetService.h>
#include <utils/KubeFormat.h>

#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
    QLabel *emptyStateLabel() {
        auto *label = new QLabel("There is nothing to display here\nNo resources found.");
        label->setAlignment(Qt::AlignCenter);
        return label;
    }

    // A service selects a pod when every key/value pair in its selector is
    // present in the pod's labels -- true here of the replica set's own pod
    // template labels, since every pod it creates carries them.
    bool selectorMatchesLabels(const QJsonObject &selector, const QJsonObject &labels) {
        if (selector.isEmpty()) return false;
        for (auto it = selector.begin(); it != selector.end(); ++it) {
            if (labels[it.key()].toString() != it.value().toString()) return false;
        }
        return true;
    }

    QString formatServicePorts(const QJsonArray &ports) {
        QStringList parts;
        for (const auto &portValue: ports) {
            const QJsonObject port = portValue.toObject();
            parts << QString("%1/%2").arg(port["port"].toInt()).arg(port["protocol"].toString());
        }
        return parts.join(", ");
    }
} // namespace

void ReplicaSetDetailsDialog::Show(QWidget *parent, const QString &context, const QString &name, const QString &ns) {
    QJsonObject replicaSet;
    QJsonArray pods;
    QJsonArray services;
    QJsonArray events;
    {
        BusyGuard busyGuard;

        KubeNetService &svc = KubeNetService::forContext(context);
        if (!svc.IsValid()) {
            QMessageBox::warning(parent, "ReplicaSet details failed", svc.LastError());
            return;
        }

        replicaSet = svc.fetchObject(KubeNetService::ResourcePath("replicasets", ns) + "/" + name);
        if (replicaSet.isEmpty()) {
            QMessageBox::warning(parent, "ReplicaSet details failed", svc.LastError());
            return;
        }

        const QJsonObject matchLabels = replicaSet["spec"].toObject()["selector"].toObject()["matchLabels"].toObject();
        if (!matchLabels.isEmpty()) {
            QStringList selectorParts;
            for (auto it = matchLabels.begin(); it != matchLabels.end(); ++it) {
                selectorParts << it.key() + "=" + it.value().toString();
            }
            pods = svc.fetchItems(KubeNetService::ResourcePath("pods", ns) + "?labelSelector=" + selectorParts.join(","));
        }

        services = svc.fetchItems(KubeNetService::ResourcePath("services", ns));

        events = svc.fetchItems(KubeNetService::ResourcePath("events", ns) + "?fieldSelector=involvedObject.name=" + name + ",involvedObject.kind=ReplicaSet");
    }

    const QJsonObject metadata = replicaSet["metadata"].toObject();
    const QJsonObject spec = replicaSet["spec"].toObject();
    const QJsonObject status = replicaSet["status"].toObject();
    const QJsonObject podTemplateSpec = spec["template"].toObject()["spec"].toObject();
    const QJsonObject podTemplateLabels = spec["template"].toObject()["metadata"].toObject()["labels"].toObject();

    QString owner;
    if (const QJsonArray ownerReferences = metadata["ownerReferences"].toArray(); !ownerReferences.isEmpty()) {
        const QJsonObject ownerRef = ownerReferences[0].toObject();
        owner = ownerRef["kind"].toString().toLower() + "/" + ownerRef["name"].toString();
    }

    QStringList images;
    for (const auto &containerValue: podTemplateSpec["containers"].toArray()) {
        images << containerValue.toObject()["image"].toString();
    }

    QJsonArray matchingServices;
    for (const auto &serviceValue: services) {
        const QJsonObject service = serviceValue.toObject();
        if (selectorMatchesLabels(service["spec"].toObject()["selector"].toObject(), podTemplateLabels)) {
            matchingServices.append(service);
        }
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("ReplicaSet: " + ns + "/" + name);
    dialog.resize(1100, 800);

    auto *content = new QWidget();
    auto *layout = new QVBoxLayout(content);

    auto *metaBox = new QGroupBox("Metadata");
    auto *metaForm = new QFormLayout(metaBox);
    metaForm->addRow("Name", new QLabel(metadata["name"].toString()));
    metaForm->addRow("Namespace", new QLabel(metadata["namespace"].toString()));
    metaForm->addRow("Created", new QLabel(KubeFormat::formatCreated(metadata["creationTimestamp"].toString())));
    metaForm->addRow("Age", new QLabel(KubeFormat::computeAge(metadata["creationTimestamp"].toString())));
    metaForm->addRow("UID", new QLabel(metadata["uid"].toString()));
    if (!owner.isEmpty()) {
        metaForm->addRow("Owner", new QLabel(owner));
    }
    auto *labelsValue = new QLabel(KubeFormat::joinKeyValues(metadata["labels"].toObject()));
    labelsValue->setWordWrap(true);
    metaForm->addRow("Labels", labelsValue);
    auto *annotationsValue = new QLabel(KubeFormat::joinKeyValues(metadata["annotations"].toObject()));
    annotationsValue->setWordWrap(true);
    metaForm->addRow("Annotations", annotationsValue);
    layout->addWidget(metaBox);

    auto *resourceBox = new QGroupBox("Resource information");
    auto *resourceForm = new QFormLayout(resourceBox);
    auto *selectorValue = new QLabel(KubeFormat::joinKeyValues(spec["selector"].toObject()["matchLabels"].toObject()));
    selectorValue->setWordWrap(true);
    resourceForm->addRow("Selector", selectorValue);
    auto *imagesValue = new QLabel(images.join("\n"));
    imagesValue->setWordWrap(true);
    resourceForm->addRow("Images", imagesValue);
    layout->addWidget(resourceBox);

    auto *podsStatusBox = new QGroupBox("Pods status");
    auto *podsStatusForm = new QFormLayout(podsStatusBox);
    podsStatusForm->addRow("Running", new QLabel(QString::number(status["readyReplicas"].toInt())));
    podsStatusForm->addRow("Desired", new QLabel(QString::number(spec["replicas"].toInt(1))));
    layout->addWidget(podsStatusBox);

    auto *podsBox = new QGroupBox("Pods");
    auto *podsLayout = new QVBoxLayout(podsBox);
    if (pods.isEmpty()) {
        podsLayout->addWidget(emptyStateLabel());
    } else {
        auto *podsTable = KubeFormat::makeDetailTable({"Name", "Images", "Node", "Status", "Restarts", "Created"});
        podsTable->setRowCount(pods.size());
        for (int i = 0; i < pods.size(); ++i) {
            const QJsonObject pod = pods[i].toObject();
            const QJsonObject podMeta = pod["metadata"].toObject();
            const QJsonObject podStatus = pod["status"].toObject();
            const QJsonObject podSpec = pod["spec"].toObject();

            QStringList podImages;
            for (const auto &containerValue: podSpec["containers"].toArray()) {
                podImages << containerValue.toObject()["image"].toString();
            }
            int restarts = 0;
            for (const auto &containerStatusValue: podStatus["containerStatuses"].toArray()) {
                restarts += containerStatusValue.toObject()["restartCount"].toInt();
            }

            podsTable->setItem(i, 0, new QTableWidgetItem(podMeta["name"].toString()));
            podsTable->setItem(i, 1, new QTableWidgetItem(podImages.join(", ")));
            podsTable->setItem(i, 2, new QTableWidgetItem(podSpec["nodeName"].toString()));
            podsTable->setItem(i, 3, new QTableWidgetItem(podStatus["phase"].toString()));
            podsTable->setItem(i, 4, new QTableWidgetItem(QString::number(restarts)));
            podsTable->setItem(i, 5, new QTableWidgetItem(KubeFormat::computeAge(podMeta["creationTimestamp"].toString())));
        }
        podsLayout->addWidget(podsTable);
    }
    layout->addWidget(podsBox);

    auto *servicesBox = new QGroupBox("Services");
    auto *servicesLayout = new QVBoxLayout(servicesBox);
    if (matchingServices.isEmpty()) {
        servicesLayout->addWidget(emptyStateLabel());
    } else {
        auto *servicesTable = KubeFormat::makeDetailTable({"Name", "Type", "Cluster IP", "Ports", "Age"});
        servicesTable->setRowCount(matchingServices.size());
        for (int i = 0; i < matchingServices.size(); ++i) {
            const QJsonObject service = matchingServices[i].toObject();
            const QJsonObject serviceMeta = service["metadata"].toObject();
            const QJsonObject serviceSpec = service["spec"].toObject();
            servicesTable->setItem(i, 0, new QTableWidgetItem(serviceMeta["name"].toString()));
            servicesTable->setItem(i, 1, new QTableWidgetItem(serviceSpec["type"].toString()));
            servicesTable->setItem(i, 2, new QTableWidgetItem(serviceSpec["clusterIP"].toString()));
            servicesTable->setItem(i, 3, new QTableWidgetItem(formatServicePorts(serviceSpec["ports"].toArray())));
            servicesTable->setItem(i, 4, new QTableWidgetItem(KubeFormat::computeAge(serviceMeta["creationTimestamp"].toString())));
        }
        servicesLayout->addWidget(servicesTable);
    }
    layout->addWidget(servicesBox);

    auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
    auto *eventsLayout = new QVBoxLayout(eventsBox);
    auto *eventsTable = KubeFormat::makeDetailTable({"Type", "Reason", "Message", "Age"});
    eventsTable->setRowCount(events.size());
    for (int i = 0; i < events.size(); ++i) {
        const QJsonObject event = events[i].toObject();
        eventsTable->setItem(i, 0, new QTableWidgetItem(event["type"].toString()));
        eventsTable->setItem(i, 1, new QTableWidgetItem(event["reason"].toString()));
        eventsTable->setItem(i, 2, new QTableWidgetItem(event["message"].toString()));
        eventsTable->setItem(i, 3, new QTableWidgetItem(KubeFormat::computeAge(event["lastTimestamp"].toString())));
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
