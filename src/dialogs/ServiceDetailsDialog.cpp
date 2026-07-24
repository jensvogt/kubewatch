#include <dialogs/ServiceDetailsDialog.h>

#include <kubectl/KubectlClient.h>
#include <utils/KubeFormat.h>

#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

void ServiceDetailsDialog::Show(QWidget *parent, const QStringList &baseArgs, const QString &name, const QString &ns) {
    QJsonObject service;
    QJsonArray endpointSubsets;
    QJsonArray pods;
    QJsonArray matchingIngresses;
    QJsonArray events;
    {
        BusyGuard busyGuard;

        QStringList getArgs = baseArgs;
        getArgs << "get" << "services" << name << "-n" << ns << "-o" << "json";
        const KubectlResult serviceResult = KubectlClient::runKubectlCommand(getArgs);
        if (!serviceResult.success) {
            QMessageBox::warning(parent, "Service details failed", serviceResult.error);
            return;
        }
        service = QJsonDocument::fromJson(serviceResult.output.toUtf8()).object();

        QStringList endpointArgs = baseArgs;
        endpointArgs << "get" << "endpoints" << name << "-n" << ns << "-o" << "json";
        const KubectlResult endpointResult = KubectlClient::runKubectlCommand(endpointArgs);
        if (endpointResult.success) {
            endpointSubsets = QJsonDocument::fromJson(endpointResult.output.toUtf8()).object()["subsets"].toArray();
        }

        if (const QJsonObject selector = service["spec"].toObject()["selector"].toObject(); !selector.isEmpty()) {
            QStringList selectorParts;
            for (auto it = selector.begin(); it != selector.end(); ++it) {
                selectorParts << it.key() + "=" + it.value().toString();
            }
            QStringList podArgs = baseArgs;
            podArgs << "get" << "pods" << "-n" << ns << "-l" << selectorParts.join(",") << "-o" << "json";
            pods = KubectlClient::fetchItems(podArgs);
        }

        QStringList ingressArgs = baseArgs;
        ingressArgs << "get" << "ingresses" << "-n" << ns << "-o" << "json";
        for (const auto &ingressValue: KubectlClient::fetchItems(ingressArgs)) {
            const QJsonObject ingress = ingressValue.toObject();
            const QJsonObject ingressSpec = ingress["spec"].toObject();
            bool referencesService =
                    ingressSpec["defaultBackend"].toObject()["service"].toObject()["name"].toString() == name;
            for (const auto &ruleValue: ingressSpec["rules"].toArray()) {
                for (const auto &pathValue: ruleValue.toObject()["http"].toObject()["paths"].toArray()) {
                    if (pathValue.toObject()["backend"].toObject()["service"].toObject()["name"].toString() ==
                        name) {
                        referencesService = true;
                    }
                }
            }
            if (referencesService) {
                matchingIngresses.append(ingress);
            }
        }

        QStringList eventArgs = baseArgs;
        eventArgs << "get" << "events" << "-n" << ns << "--field-selector"
                << "involvedObject.name=" + name + ",involvedObject.kind=Service" << "-o" << "json";
        events = KubectlClient::fetchItems(eventArgs);
    }

    const QJsonObject metadata = service["metadata"].toObject();
    const QJsonObject spec = service["spec"].toObject();

    QDialog dialog(parent);
    dialog.setWindowTitle("Service: " + ns + "/" + name);
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
    resourceForm->addRow("Type", new QLabel(spec["type"].toString()));
    resourceForm->addRow("Cluster IP", new QLabel(spec["clusterIP"].toString()));
    resourceForm->addRow("Session Affinity", new QLabel(spec["sessionAffinity"].toString()));
    auto *selectorValue = new QLabel(KubeFormat::joinKeyValues(spec["selector"].toObject()));
    selectorValue->setWordWrap(true);
    resourceForm->addRow("Selector", selectorValue);
    layout->addWidget(resourceBox);

    auto *endpointsBox = new QGroupBox("Endpoints");
    auto *endpointsLayout = new QVBoxLayout(endpointsBox);
    QList<QStringList> endpointRows;
    for (const auto &subsetValue: endpointSubsets) {
        const QJsonObject subset = subsetValue.toObject();
        QStringList ips;
        for (const auto &addressValue: subset["addresses"].toArray()) {
            ips << addressValue.toObject()["ip"].toString();
        }
        for (const auto &portValue: subset["ports"].toArray()) {
            const QJsonObject port = portValue.toObject();
            for (const QString &ip: ips) {
                endpointRows.append(
                    {ip, QString::number(port["port"].toInt()), port["protocol"].toString()});
            }
        }
    }
    if (endpointRows.isEmpty()) {
        auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
        emptyLabel->setAlignment(Qt::AlignCenter);
        endpointsLayout->addWidget(emptyLabel);
    } else {
        auto *endpointsTable = KubeFormat::makeDetailTable({"IP", "Port", "Protocol"});
        endpointsTable->setRowCount(endpointRows.size());
        for (int i = 0; i < endpointRows.size(); ++i) {
            endpointsTable->setItem(i, 0, new QTableWidgetItem(endpointRows[i][0]));
            endpointsTable->setItem(i, 1, new QTableWidgetItem(endpointRows[i][1]));
            endpointsTable->setItem(i, 2, new QTableWidgetItem(endpointRows[i][2]));
        }
        endpointsLayout->addWidget(endpointsTable);
    }
    layout->addWidget(endpointsBox);

    auto *podsBox = new QGroupBox("Pods");
    auto *podsLayout = new QVBoxLayout(podsBox);
    if (pods.isEmpty()) {
        auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
        emptyLabel->setAlignment(Qt::AlignCenter);
        podsLayout->addWidget(emptyLabel);
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

    auto *ingressesBox = new QGroupBox("Ingresses");
    auto *ingressesLayout = new QVBoxLayout(ingressesBox);
    if (matchingIngresses.isEmpty()) {
        auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
        emptyLabel->setAlignment(Qt::AlignCenter);
        ingressesLayout->addWidget(emptyLabel);
    } else {
        auto *ingressesTable = KubeFormat::makeDetailTable({"Name", "Hosts", "Created"});
        ingressesTable->setRowCount(matchingIngresses.size());
        for (int i = 0; i < matchingIngresses.size(); ++i) {
            const QJsonObject ingress = matchingIngresses[i].toObject();
            const QJsonObject ingressMeta = ingress["metadata"].toObject();
            QStringList hosts;
            for (const auto &ruleValue: ingress["spec"].toObject()["rules"].toArray()) {
                hosts << ruleValue.toObject()["host"].toString();
            }
            ingressesTable->setItem(i, 0, new QTableWidgetItem(ingressMeta["name"].toString()));
            ingressesTable->setItem(i, 1, new QTableWidgetItem(hosts.join(", ")));
            ingressesTable->setItem(
                i, 2, new QTableWidgetItem(KubeFormat::computeAge(ingressMeta["creationTimestamp"].toString())));
        }
        ingressesLayout->addWidget(ingressesTable);
    }
    layout->addWidget(ingressesBox);

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
