#include <dialogs/DeploymentDetailsDialog.h>

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

    bool ownedByDeployment(const QJsonObject &replicaSet, const QString &deploymentName) {
        for (const auto &ownerValue: replicaSet["metadata"].toObject()["ownerReferences"].toArray()) {
            const QJsonObject owner = ownerValue.toObject();
            if (owner["kind"].toString() == "Deployment" && owner["name"].toString() == deploymentName) {
                return true;
            }
        }
        return false;
    }

    QString revisionOf(const QJsonObject &obj) {
        return obj["metadata"].toObject()["annotations"].toObject()["deployment.kubernetes.io/revision"].toString();
    }

    QString replicaSetPods(const QJsonObject &replicaSet) {
        const int ready = replicaSet["status"].toObject()["readyReplicas"].toInt();
        const int desired = replicaSet["spec"].toObject()["replicas"].toInt();
        return QString("%1/%2").arg(ready).arg(desired);
    }

    QStringList replicaSetImages(const QJsonObject &replicaSet) {
        QStringList images;
        for (const auto &containerValue:
             replicaSet["spec"].toObject()["template"].toObject()["spec"].toObject()["containers"].toArray()) {
            images << containerValue.toObject()["image"].toString();
        }
        return images;
    }
} // namespace

void DeploymentDetailsDialog::Show(QWidget *parent, const QString &context, const QString &name, const QString &ns) {
    QJsonObject deployment;
    QJsonArray replicaSets;
    QJsonArray horizontalPodAutoscalers;
    QJsonArray events;
    {
        BusyGuard busyGuard;

        KubeNetService &svc = KubeNetService::forContext(context);
        if (!svc.IsValid()) {
            QMessageBox::warning(parent, "Deployment details failed", svc.LastError());
            return;
        }

        deployment = svc.fetchObject(KubeNetService::ResourcePath("deployments", ns) + "/" + name);
        if (deployment.isEmpty()) {
            QMessageBox::warning(parent, "Deployment details failed", svc.LastError());
            return;
        }

        replicaSets = svc.fetchItems(KubeNetService::ResourcePath("replicasets", ns));

        horizontalPodAutoscalers = svc.fetchItems(KubeNetService::ResourcePath("horizontalpodautoscalers", ns));

        events = svc.fetchItems(KubeNetService::ResourcePath("events", ns) + "?fieldSelector=involvedObject.name=" + name + ",involvedObject.kind=Deployment");
    }

    const QJsonObject metadata = deployment["metadata"].toObject();
    const QJsonObject status = deployment["status"].toObject();
    const QJsonObject spec = deployment["spec"].toObject();
    const QJsonObject strategy = spec["strategy"].toObject();

    const QString currentRevision = revisionOf(deployment);
    QJsonObject newReplicaSet;
    QList<QJsonObject> oldReplicaSets;
    for (const auto &replicaSetValue: replicaSets) {
        const QJsonObject replicaSet = replicaSetValue.toObject();
        if (!ownedByDeployment(replicaSet, name)) continue;
        if (!currentRevision.isEmpty() && revisionOf(replicaSet) == currentRevision) {
            newReplicaSet = replicaSet;
        } else {
            oldReplicaSets.append(replicaSet);
        }
    }

    QJsonArray matchingHpas;
    for (const auto &hpaValue: horizontalPodAutoscalers) {
        const QJsonObject hpa = hpaValue.toObject();
        const QJsonObject scaleTarget = hpa["spec"].toObject()["scaleTargetRef"].toObject();
        if (scaleTarget["kind"].toString() == "Deployment" && scaleTarget["name"].toString() == name) {
            matchingHpas.append(hpa);
        }
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("Deployment: " + ns + "/" + name);
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
    auto *labelsValue = new QLabel(KubeFormat::joinKeyValues(metadata["labels"].toObject()));
    labelsValue->setWordWrap(true);
    metaForm->addRow("Labels", labelsValue);
    auto *annotationsValue = new QLabel(KubeFormat::joinKeyValues(metadata["annotations"].toObject()));
    annotationsValue->setWordWrap(true);
    metaForm->addRow("Annotations", annotationsValue);
    layout->addWidget(metaBox);

    auto *resourceBox = new QGroupBox("Resource information");
    auto *resourceForm = new QFormLayout(resourceBox);
    resourceForm->addRow("Strategy", new QLabel(strategy["type"].toString()));
    resourceForm->addRow("Min ready seconds", new QLabel(QString::number(spec["minReadySeconds"].toInt())));
    resourceForm->addRow("Revision history limit", new QLabel(QString::number(spec["revisionHistoryLimit"].toInt())));
    auto *selectorValue = new QLabel(KubeFormat::joinKeyValues(spec["selector"].toObject()["matchLabels"].toObject()));
    selectorValue->setWordWrap(true);
    resourceForm->addRow("Selector", selectorValue);
    layout->addWidget(resourceBox);

    auto *rollingUpdateBox = new QGroupBox("Rolling update strategy");
    auto *rollingUpdateLayout = new QVBoxLayout(rollingUpdateBox);
    if (strategy["type"].toString() == "RollingUpdate") {
        const QJsonObject rollingUpdate = strategy["rollingUpdate"].toObject();
        auto *rollingUpdateForm = new QFormLayout();
        rollingUpdateForm->addRow("Max surge", new QLabel(rollingUpdate["maxSurge"].toVariant().toString()));
        rollingUpdateForm->addRow("Max unavailable", new QLabel(rollingUpdate["maxUnavailable"].toVariant().toString()));
        rollingUpdateLayout->addLayout(rollingUpdateForm);
    } else {
        rollingUpdateLayout->addWidget(new QLabel("Not applicable for the '" + strategy["type"].toString() + "' strategy."));
    }
    layout->addWidget(rollingUpdateBox);

    auto *podsStatusBox = new QGroupBox("Pods status");
    auto *podsStatusForm = new QFormLayout(podsStatusBox);
    podsStatusForm->addRow("Desired", new QLabel(QString::number(spec["replicas"].toInt(1))));
    podsStatusForm->addRow("Current", new QLabel(QString::number(status["replicas"].toInt())));
    podsStatusForm->addRow("Updated", new QLabel(QString::number(status["updatedReplicas"].toInt())));
    podsStatusForm->addRow("Ready", new QLabel(QString::number(status["readyReplicas"].toInt())));
    podsStatusForm->addRow("Available", new QLabel(QString::number(status["availableReplicas"].toInt())));
    podsStatusForm->addRow("Unavailable", new QLabel(QString::number(status["unavailableReplicas"].toInt())));
    layout->addWidget(podsStatusBox);

    auto *conditionsBox = new QGroupBox("Conditions");
    auto *conditionsLayout = new QVBoxLayout(conditionsBox);
    auto *conditionsTable =
            KubeFormat::makeDetailTable({"Type", "Status", "Last Update", "Last Transition", "Reason", "Message"});
    const QJsonArray conditions = status["conditions"].toArray();
    conditionsTable->setRowCount(conditions.size());
    for (int i = 0; i < conditions.size(); ++i) {
        const QJsonObject condition = conditions[i].toObject();
        conditionsTable->setItem(i, 0, new QTableWidgetItem(condition["type"].toString()));
        conditionsTable->setItem(i, 1, new QTableWidgetItem(condition["status"].toString()));
        conditionsTable->setItem(i, 2, new QTableWidgetItem(KubeFormat::formatCreated(condition["lastUpdateTime"].toString())));
        conditionsTable->setItem(i, 3,
                                 new QTableWidgetItem(KubeFormat::formatCreated(condition["lastTransitionTime"].toString())));
        conditionsTable->setItem(i, 4, new QTableWidgetItem(condition["reason"].toString()));
        conditionsTable->setItem(i, 5, new QTableWidgetItem(condition["message"].toString()));
    }
    conditionsLayout->addWidget(conditionsTable);
    layout->addWidget(conditionsBox);

    auto *newReplicaSetBox = new QGroupBox("New Replica Set");
    auto *newReplicaSetLayout = new QVBoxLayout(newReplicaSetBox);
    if (newReplicaSet.isEmpty()) {
        newReplicaSetLayout->addWidget(new QLabel("No new replica set found."));
    } else {
        const QJsonObject rsMeta = newReplicaSet["metadata"].toObject();
        auto *rsForm = new QFormLayout();
        rsForm->addRow("Name", new QLabel(rsMeta["name"].toString()));
        rsForm->addRow("Namespace", new QLabel(rsMeta["namespace"].toString()));
        rsForm->addRow("Age", new QLabel(KubeFormat::computeAge(rsMeta["creationTimestamp"].toString())));
        rsForm->addRow("Pods", new QLabel(replicaSetPods(newReplicaSet)));
        auto *rsLabelsValue = new QLabel(KubeFormat::joinKeyValues(rsMeta["labels"].toObject()));
        rsLabelsValue->setWordWrap(true);
        rsForm->addRow("Labels", rsLabelsValue);
        auto *rsImagesValue = new QLabel(replicaSetImages(newReplicaSet).join("\n"));
        rsImagesValue->setWordWrap(true);
        rsForm->addRow("Images", rsImagesValue);
        newReplicaSetLayout->addLayout(rsForm);
    }
    layout->addWidget(newReplicaSetBox);

    auto *oldReplicaSetsBox = new QGroupBox("Old Replica Sets");
    auto *oldReplicaSetsLayout = new QVBoxLayout(oldReplicaSetsBox);
    if (oldReplicaSets.isEmpty()) {
        oldReplicaSetsLayout->addWidget(emptyStateLabel());
    } else {
        auto *oldReplicaSetsTable = KubeFormat::makeDetailTable({"Name", "Pods", "Images", "Age"});
        oldReplicaSetsTable->setRowCount(oldReplicaSets.size());
        for (int i = 0; i < oldReplicaSets.size(); ++i) {
            const QJsonObject rsMeta = oldReplicaSets[i]["metadata"].toObject();
            oldReplicaSetsTable->setItem(i, 0, new QTableWidgetItem(rsMeta["name"].toString()));
            oldReplicaSetsTable->setItem(i, 1, new QTableWidgetItem(replicaSetPods(oldReplicaSets[i])));
            oldReplicaSetsTable->setItem(i, 2, new QTableWidgetItem(replicaSetImages(oldReplicaSets[i]).join(", ")));
            oldReplicaSetsTable->setItem(i, 3, new QTableWidgetItem(KubeFormat::computeAge(rsMeta["creationTimestamp"].toString())));
        }
        oldReplicaSetsLayout->addWidget(oldReplicaSetsTable);
    }
    layout->addWidget(oldReplicaSetsBox);

    auto *hpaBox = new QGroupBox(QString("Horizontal Pod Autoscalers (%1)").arg(matchingHpas.size()));
    auto *hpaLayout = new QVBoxLayout(hpaBox);
    if (matchingHpas.isEmpty()) {
        hpaLayout->addWidget(emptyStateLabel());
    } else {
        auto *hpaTable = KubeFormat::makeDetailTable({"Name", "Min Pods", "Max Pods", "Current Replicas", "Age"});
        hpaTable->setRowCount(matchingHpas.size());
        for (int i = 0; i < matchingHpas.size(); ++i) {
            const QJsonObject hpa = matchingHpas[i].toObject();
            const QJsonObject hpaMeta = hpa["metadata"].toObject();
            const QJsonObject hpaSpec = hpa["spec"].toObject();
            const QJsonObject hpaStatus = hpa["status"].toObject();
            hpaTable->setItem(i, 0, new QTableWidgetItem(hpaMeta["name"].toString()));
            hpaTable->setItem(i, 1, new QTableWidgetItem(QString::number(hpaSpec["minReplicas"].toInt(1))));
            hpaTable->setItem(i, 2, new QTableWidgetItem(QString::number(hpaSpec["maxReplicas"].toInt())));
            hpaTable->setItem(i, 3, new QTableWidgetItem(QString::number(hpaStatus["currentReplicas"].toInt())));
            hpaTable->setItem(i, 4, new QTableWidgetItem(KubeFormat::computeAge(hpaMeta["creationTimestamp"].toString())));
        }
        hpaLayout->addWidget(hpaTable);
    }
    layout->addWidget(hpaBox);

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
