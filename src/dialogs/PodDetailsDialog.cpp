#include <dialogs/PodDetailsDialog.h>

#include <kubectl/KubectlClient.h>
#include <utils/KubeFormat.h>

#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
    QGroupBox *buildContainerBox(const QJsonObject &containerSpec, const QJsonObject &containerStatus, const QJsonArray &volumes) {
        auto *box = new QGroupBox("Container: " + containerSpec["name"].toString());
        auto *layout = new QVBoxLayout(box);

        layout->addWidget(new QLabel("Image: " + containerSpec["image"].toString()));

        const QJsonObject state = containerStatus["state"].toObject();
        const QString startedAt =
                state.contains("running") ? KubeFormat::formatCreated(state["running"].toObject()["startedAt"].toString()) : "-";

        auto *statusForm = new QFormLayout();
        statusForm->addRow("Ready", new QLabel(containerStatus["ready"].toBool() ? "true" : "false"));
        statusForm->addRow("Started", new QLabel(containerStatus["started"].toBool() ? "true" : "false"));
        statusForm->addRow("Restart Count", new QLabel(QString::number(containerStatus["restartCount"].toInt())));
        statusForm->addRow("Started At", new QLabel(startedAt));
        layout->addLayout(statusForm);

        // The single most useful signal for "pod restarted but the app's own logs show
        // nothing" -- covers kills the process never got to log (OOMKilled, a failed
        // liveness probe, node-triggered SIGKILL/SIGTERM).
        if (const QJsonObject lastTerminated = containerStatus["lastState"].toObject()["terminated"].toObject();
            !lastTerminated.isEmpty()) {
            auto *lastStateBox = new QGroupBox("Last Termination");
            auto *lastStateForm = new QFormLayout(lastStateBox);
            lastStateForm->addRow("Reason", new QLabel(lastTerminated["reason"].toString()));
            lastStateForm->addRow("Exit Code", new QLabel(QString::number(lastTerminated["exitCode"].toInt())));
            if (const int signal = lastTerminated["signal"].toInt(); signal != 0) {
                lastStateForm->addRow("Signal", new QLabel(QString::number(signal)));
            }
            lastStateForm->addRow("Started At", new QLabel(KubeFormat::formatCreated(lastTerminated["startedAt"].toString())));
            lastStateForm->addRow("Finished At", new QLabel(KubeFormat::formatCreated(lastTerminated["finishedAt"].toString())));
            if (const QString message = lastTerminated["message"].toString(); !message.isEmpty()) {
                auto *messageLabel = new QLabel(message);
                messageLabel->setWordWrap(true);
                lastStateForm->addRow("Message", messageLabel);
            }
            layout->addWidget(lastStateBox);
        }

        QStringList envLines;
        for (const auto &envValue: containerSpec["env"].toArray()) {
            const QJsonObject env = envValue.toObject();
            const QString key = env["name"].toString();
            if (env.contains("value")) {
                envLines << key + "=" + env["value"].toString();
            } else if (env.contains("valueFrom")) {
                const QJsonObject valueFrom = env["valueFrom"].toObject();
                if (valueFrom.contains("secretKeyRef")) {
                    envLines << key + "=<secret:" + valueFrom["secretKeyRef"].toObject()["name"].toString() + ">";
                } else if (valueFrom.contains("configMapKeyRef")) {
                    envLines << key + "=<configmap:" + valueFrom["configMapKeyRef"].toObject()["name"].toString() + ">";
                } else if (valueFrom.contains("fieldRef")) {
                    envLines << key + "=<field:" + valueFrom["fieldRef"].toObject()["fieldPath"].toString() + ">";
                } else {
                    envLines << key + "=<from>";
                }
            }
        }
        if (!envLines.isEmpty()) {
            auto *envBox = new QGroupBox("Environment Variables");
            auto *envLayout = new QVBoxLayout(envBox);
            auto *envLabel = new QLabel(envLines.join("\n"));
            envLabel->setWordWrap(true);
            envLayout->addWidget(envLabel);
            layout->addWidget(envBox);
        }

        if (const QJsonArray mounts = containerSpec["volumeMounts"].toArray(); !mounts.isEmpty()) {
            auto *mountsTable =
                    KubeFormat::makeDetailTable({"Name", "Read Only", "Mount Path", "Sub Path", "Source Type", "Source Name"});
            mountsTable->setRowCount(mounts.size());
            for (int i = 0; i < mounts.size(); ++i) {
                const QJsonObject mount = mounts[i].toObject();
                const auto [sourceType, sourceName] = KubeFormat::volumeSourceInfo(KubeFormat::findByName(volumes, mount["name"].toString()));
                mountsTable->setItem(i, 0, new QTableWidgetItem(mount["name"].toString()));
                mountsTable->setItem(i, 1, new QTableWidgetItem(mount["readOnly"].toBool() ? "true" : "false"));
                mountsTable->setItem(i, 2, new QTableWidgetItem(mount["mountPath"].toString()));
                const QString subPath = mount["subPath"].toString();
                mountsTable->setItem(i, 3, new QTableWidgetItem(subPath.isEmpty() ? "-" : subPath));
                mountsTable->setItem(i, 4, new QTableWidgetItem(sourceType));
                mountsTable->setItem(i, 5, new QTableWidgetItem(sourceName.isEmpty() ? "-" : sourceName));
            }
            auto *mountsBox = new QGroupBox("Mounts");
            auto *mountsLayout = new QVBoxLayout(mountsBox);
            mountsLayout->addWidget(mountsTable);
            layout->addWidget(mountsBox);
        }

        auto addProbe = [&](const QString &label, const QString &key) {
            if (!containerSpec.contains(key)) return;
            const QJsonObject probe = containerSpec[key].toObject();
            auto *probeBox = new QGroupBox(label);
            auto *probeForm = new QFormLayout(probeBox);
            probeForm->addRow("Initial Delay (s)", new QLabel(QString::number(probe["initialDelaySeconds"].toInt())));
            probeForm->addRow("Timeout (s)", new QLabel(QString::number(probe["timeoutSeconds"].toInt())));
            probeForm->addRow("Period (s)", new QLabel(QString::number(probe["periodSeconds"].toInt())));
            probeForm->addRow("Success Threshold", new QLabel(QString::number(probe["successThreshold"].toInt())));
            probeForm->addRow("Failure Threshold", new QLabel(QString::number(probe["failureThreshold"].toInt())));
            probeForm->addRow("Endpoint", new QLabel(KubeFormat::formatProbeEndpoint(probe)));
            layout->addWidget(probeBox);
        };
        addProbe("Liveness Probe", "livenessProbe");
        addProbe("Readiness Probe", "readinessProbe");
        addProbe("Startup Probe", "startupProbe");

        const QJsonObject resources = containerSpec["resources"].toObject();
        auto *resourcesForm = new QFormLayout();
        resourcesForm->addRow("Limits", new QLabel(KubeFormat::formatResourceList(resources["limits"].toObject())));
        resourcesForm->addRow("Requests", new QLabel(KubeFormat::formatResourceList(resources["requests"].toObject())));
        layout->addLayout(resourcesForm);

        return box;
    }
} // namespace

void PodDetailsDialog::Show(QWidget *parent, const QStringList &baseArgs, const QString &name, const QString &ns) {
    QJsonObject pod;
    QJsonArray events;
    QString ownerKind;
    QString ownerName;
    KubectlResult ownerResult;
    {
        BusyGuard busyGuard;

        QStringList getArgs = baseArgs;
        getArgs << "get" << "pods" << name << "-n" << ns << "-o" << "json";
        const auto [success, output, error] = KubectlClient::runKubectlCommand(getArgs);
        if (!success) {
            QMessageBox::warning(parent, "Pod details failed", error);
            return;
        }
        pod = QJsonDocument::fromJson(output.toUtf8()).object();

        QStringList eventArgs = baseArgs;
        eventArgs << "get" << "events" << "-n" << ns << "--field-selector"
                << "involvedObject.name=" + name + ",involvedObject.kind=Pod" << "-o" << "json";
        events = KubectlClient::fetchItems(eventArgs);

        if (const QJsonArray ownerReferences = pod["metadata"].toObject()["ownerReferences"].toArray(); !ownerReferences.isEmpty()) {
            const QJsonObject owner = ownerReferences[0].toObject();
            ownerKind = owner["kind"].toString();
            ownerName = owner["name"].toString();

            QStringList ownerArgs = baseArgs;
            ownerArgs << "get" << (ownerKind.toLower() + "s") << ownerName << "-n" << ns << "-o" << "json";
            ownerResult = KubectlClient::runKubectlCommand(ownerArgs);
        }
    }

    const QJsonObject metadata = pod["metadata"].toObject();
    const QJsonObject status = pod["status"].toObject();
    const QJsonObject spec = pod["spec"].toObject();

    QDialog dialog(parent);
    dialog.setWindowTitle("Pod: " + ns + "/" + name);
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
    resourceForm->addRow("Node", new QLabel(spec["nodeName"].toString()));
    resourceForm->addRow("Status", new QLabel(status["phase"].toString()));
    resourceForm->addRow("Pod IP", new QLabel(status["podIP"].toString()));
    resourceForm->addRow("QoS Class", new QLabel(status["qosClass"].toString()));
    int totalRestarts = 0;
    for (const auto &containerStatusValue: status["containerStatuses"].toArray()) {
        totalRestarts += containerStatusValue.toObject()["restartCount"].toInt();
    }
    resourceForm->addRow("Restarts", new QLabel(QString::number(totalRestarts)));
    resourceForm->addRow("Service Account", new QLabel(spec["serviceAccountName"].toString()));
    QStringList pullSecrets;
    for (const auto &secretValue: spec["imagePullSecrets"].toArray()) {
        pullSecrets << secretValue.toObject()["name"].toString();
    }
    resourceForm->addRow("Image Pull Secrets", new QLabel(pullSecrets.isEmpty() ? "-" : pullSecrets.join(", ")));
    layout->addWidget(resourceBox);

    auto *conditionsBox = new QGroupBox("Conditions");
    auto *conditionsLayout = new QVBoxLayout(conditionsBox);
    auto *conditionsTable = KubeFormat::makeDetailTable({"Type", "Status", "Last Probe", "Last Transition", "Reason", "Message"});
    const QJsonArray conditions = status["conditions"].toArray();
    conditionsTable->setRowCount(conditions.size());
    for (int i = 0; i < conditions.size(); ++i) {
        const QJsonObject condition = conditions[i].toObject();
        conditionsTable->setItem(i, 0, new QTableWidgetItem(condition["type"].toString()));
        conditionsTable->setItem(i, 1, new QTableWidgetItem(condition["status"].toString()));
        conditionsTable->setItem(i, 2, new QTableWidgetItem(KubeFormat::formatCreated(condition["lastProbeTime"].toString())));
        conditionsTable->setItem(i, 3,
                                 new QTableWidgetItem(KubeFormat::formatCreated(condition["lastTransitionTime"].toString())));
        conditionsTable->setItem(i, 4, new QTableWidgetItem(condition["reason"].toString()));
        conditionsTable->setItem(i, 5, new QTableWidgetItem(condition["message"].toString()));
    }
    conditionsLayout->addWidget(conditionsTable);
    layout->addWidget(conditionsBox);

    auto *controlledByBox = new QGroupBox("Controlled by");
    auto *controlledByLayout = new QVBoxLayout(controlledByBox);
    if (ownerName.isEmpty()) {
        controlledByLayout->addWidget(new QLabel("Not controlled by another resource."));
    } else {
        auto *ownerForm = new QFormLayout();
        ownerForm->addRow("Name", new QLabel(ownerName));
        ownerForm->addRow("Kind", new QLabel(ownerKind));

        if (ownerResult.success) {
            const QJsonObject ownerObj = QJsonDocument::fromJson(ownerResult.output.toUtf8()).object();
            const QJsonObject ownerMeta = ownerObj["metadata"].toObject();
            const QJsonObject ownerStatus = ownerObj["status"].toObject();
            const QJsonObject ownerSpec = ownerObj["spec"].toObject();

            const QString desired = ownerSpec.contains("replicas")
                                        ? QString::number(ownerSpec["replicas"].toInt())
                                        : ownerSpec.contains("completions")
                                              ? QString::number(ownerSpec["completions"].toInt())
                                              : "-";
            const QString ready = ownerStatus.contains("readyReplicas")
                                      ? QString::number(ownerStatus["readyReplicas"].toInt())
                                      : ownerStatus.contains("succeeded")
                                            ? QString::number(ownerStatus["succeeded"].toInt())
                                            : "-";
            ownerForm->addRow("Pods", new QLabel(ready + " / " + desired));
            ownerForm->addRow("Age", new QLabel(KubeFormat::computeAge(ownerMeta["creationTimestamp"].toString())));

            auto *ownerLabelsValue = new QLabel(KubeFormat::joinKeyValues(ownerMeta["labels"].toObject()));
            ownerLabelsValue->setWordWrap(true);
            ownerForm->addRow("Labels", ownerLabelsValue);

            QStringList ownerImages;
            for (const auto &containerValue:
                 ownerSpec["template"].toObject()["spec"].toObject()["containers"].toArray()) {
                ownerImages << containerValue.toObject()["image"].toString();
            }
            auto *ownerImagesLabel = new QLabel(ownerImages.join("\n"));
            ownerImagesLabel->setWordWrap(true);
            ownerForm->addRow("Images", ownerImagesLabel);
        }
        controlledByLayout->addLayout(ownerForm);
    }
    layout->addWidget(controlledByBox);

    auto *pvcBox = new QGroupBox("Persistent Volume Claims");
    auto *pvcLayout = new QVBoxLayout(pvcBox);
    QStringList pvcNames;
    for (const auto &volumeValue: spec["volumes"].toArray()) {
        if (const QJsonObject volume = volumeValue.toObject(); volume.contains("persistentVolumeClaim")) {
            pvcNames << volume["persistentVolumeClaim"].toObject()["claimName"].toString();
        }
    }
    if (pvcNames.isEmpty()) {
        pvcLayout->addWidget(new QLabel("There is nothing to display here."));
    } else {
        auto *pvcTable = KubeFormat::makeDetailTable({"Claim Name"});
        pvcTable->setRowCount(pvcNames.size());
        for (int i = 0; i < pvcNames.size(); ++i) {
            pvcTable->setItem(i, 0, new QTableWidgetItem(pvcNames[i]));
        }
        pvcLayout->addWidget(pvcTable);
    }
    layout->addWidget(pvcBox);

    auto *eventsBox = new QGroupBox(QString("Events (%1)").arg(events.size()));
    auto *eventsLayout = new QVBoxLayout(eventsBox);
    auto *eventsTable = KubeFormat::makeDetailTable({"Reason", "Message", "Count", "First Seen", "Last Seen"});
    eventsTable->setRowCount(events.size());
    for (int i = 0; i < events.size(); ++i) {
        const QJsonObject event = events[i].toObject();
        eventsTable->setItem(i, 0, new QTableWidgetItem(event["reason"].toString()));
        eventsTable->setItem(i, 1, new QTableWidgetItem(event["message"].toString()));
        eventsTable->setItem(i, 2, new QTableWidgetItem(QString::number(event["count"].toInt())));
        eventsTable->setItem(i, 3, new QTableWidgetItem(KubeFormat::computeAge(event["firstTimestamp"].toString())));
        eventsTable->setItem(i, 4, new QTableWidgetItem(KubeFormat::computeAge(event["lastTimestamp"].toString())));
    }
    eventsLayout->addWidget(eventsTable);
    layout->addWidget(eventsBox);

    const QJsonArray containerStatuses = status["containerStatuses"].toArray();
    const QJsonArray volumes = spec["volumes"].toArray();
    auto *containersBox = new QGroupBox("Containers");
    auto *containersLayout = new QVBoxLayout(containersBox);
    for (const auto &containerValue: spec["containers"].toArray()) {
        const QJsonObject containerSpec = containerValue.toObject();
        const QJsonObject containerStatus = KubeFormat::findByName(containerStatuses, containerSpec["name"].toString());
        containersLayout->addWidget(buildContainerBox(containerSpec, containerStatus, volumes));
    }
    layout->addWidget(containersBox);

    auto *scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(content);

    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->addWidget(scrollArea);

    dialog.exec();
}
