#include <dialogs/IngressDetailsDialog.h>

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

void IngressDetailsDialog::Show(QWidget *parent, const QStringList &baseArgs, const QString &name, const QString &ns) {
    QJsonObject ingress;
    QJsonArray events;
    {
        BusyGuard busyGuard;
        QStringList getArgs = baseArgs;
        getArgs << "get" << "ingresses" << name << "-n" << ns << "-o" << "json";
        const KubectlResult ingressResult = KubectlClient::runKubectlCommand(getArgs);
        if (!ingressResult.success) {
            QMessageBox::warning(parent, "Ingress details failed", ingressResult.error);
            return;
        }
        ingress = QJsonDocument::fromJson(ingressResult.output.toUtf8()).object();

        QStringList eventArgs = baseArgs;
        eventArgs << "get" << "events" << "-n" << ns << "--field-selector"
                  << "involvedObject.name=" + name + ",involvedObject.kind=Ingress" << "-o" << "json";
        events = KubectlClient::fetchItems(eventArgs);
    }

    const QJsonObject metadata = ingress["metadata"].toObject();
    const QJsonObject spec = ingress["spec"].toObject();
    const QJsonObject status = ingress["status"].toObject();

    QDialog dialog(parent);
    dialog.setWindowTitle("Ingress: " + ns + "/" + name);
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
    resourceForm->addRow("Ingress Class Name", new QLabel(spec["ingressClassName"].toString()));
    QStringList endpoints;
    for (const auto &lbValue: status["loadBalancer"].toObject()["ingress"].toArray()) {
        const QJsonObject lb = lbValue.toObject();
        const QString host = lb["hostname"].toString();
        endpoints << (!host.isEmpty() ? host : lb["ip"].toString());
    }
    auto *endpointsLabel = new QLabel(endpoints.isEmpty() ? "-" : endpoints.join("\n"));
    endpointsLabel->setWordWrap(true);
    resourceForm->addRow("Endpoints", endpointsLabel);
    layout->addWidget(resourceBox);

    auto *rulesBox = new QGroupBox("Rules");
    auto *rulesLayout = new QVBoxLayout(rulesBox);

    QHash<QString, QString> tlsSecretByHost;
    for (const auto &tlsValue: spec["tls"].toArray()) {
        const QJsonObject tls = tlsValue.toObject();
        const QString secretName = tls["secretName"].toString();
        for (const auto &hostValue: tls["hosts"].toArray()) {
            tlsSecretByHost[hostValue.toString()] = secretName;
        }
    }

    struct RuleRow {
        QString host, path, pathType, serviceName, servicePort, tlsSecret;
    };
    QList<RuleRow> ruleRows;
    for (const auto &ruleValue: spec["rules"].toArray()) {
        const QJsonObject rule = ruleValue.toObject();
        const QString host = rule["host"].toString();
        for (const auto &pathValue: rule["http"].toObject()["paths"].toArray()) {
            const QJsonObject pathObj = pathValue.toObject();
            const QJsonObject backendService = pathObj["backend"].toObject()["service"].toObject();
            const QJsonValue portValue = backendService["port"].toObject()["number"];
            ruleRows.append({host, pathObj["path"].toString(), pathObj["pathType"].toString(),
                              backendService["name"].toString(),
                              portValue.isDouble() ? QString::number(portValue.toInt()) : QStringLiteral("-"),
                              tlsSecretByHost.value(host, "-")});
        }
    }

    if (ruleRows.isEmpty()) {
        auto *emptyLabel = new QLabel("There is nothing to display here\nNo resources found.");
        emptyLabel->setAlignment(Qt::AlignCenter);
        rulesLayout->addWidget(emptyLabel);
    } else {
        auto *rulesTable = KubeFormat::makeDetailTable({"Host", "Path", "Path Type", "Service Name", "Service Port", "TLS Secret"});
        rulesTable->setRowCount(ruleRows.size());
        for (int i = 0; i < ruleRows.size(); ++i) {
            const RuleRow &r = ruleRows[i];
            rulesTable->setItem(i, 0, new QTableWidgetItem(r.host));
            rulesTable->setItem(i, 1, new QTableWidgetItem(r.path));
            rulesTable->setItem(i, 2, new QTableWidgetItem(r.pathType));
            rulesTable->setItem(i, 3, new QTableWidgetItem(r.serviceName));
            rulesTable->setItem(i, 4, new QTableWidgetItem(r.servicePort));
            rulesTable->setItem(i, 5, new QTableWidgetItem(r.tlsSecret));
        }
        rulesLayout->addWidget(rulesTable);
    }
    layout->addWidget(rulesBox);

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
