#include <dialogs/JobDetailsDialog.h>

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

void JobDetailsDialog::Show(QWidget *parent, const QStringList &baseArgs, const QString &name, const QString &ns) {
    QJsonObject job;
    QJsonArray pods;
    QJsonArray events;
    {
        BusyGuard busyGuard;

        QStringList getArgs = baseArgs;
        getArgs << "get" << "jobs" << name << "-n" << ns << "-o" << "json";
        const auto [success, output, error] = KubectlClient::runKubectlCommand(getArgs);
        if (!success) {
            QMessageBox::warning(parent, "Job details failed", error);
            return;
        }
        job = QJsonDocument::fromJson(output.toUtf8()).object();

        QStringList podArgs = baseArgs;
        podArgs << "get" << "pods" << "-n" << ns << "-l" << "job-name=" + name << "-o" << "json";
        pods = KubectlClient::fetchItems(podArgs);

        QStringList eventArgs = baseArgs;
        eventArgs << "get" << "events" << "-n" << ns << "--field-selector"
                << "involvedObject.name=" + name + ",involvedObject.kind=Job" << "-o" << "json";
        events = KubectlClient::fetchItems(eventArgs);
    }

    const QJsonObject metadata = job["metadata"].toObject();
    const QJsonObject status = job["status"].toObject();
    const QJsonObject spec = job["spec"].toObject();
    const QJsonValue completions = spec["completions"];
    const QJsonValue parallelism = spec["parallelism"];
    const QString desired = completions.isDouble()
                                ? QString::number(completions.toInt())
                                : parallelism.isDouble()
                                      ? QString::number(parallelism.toInt())
                                      : QStringLiteral("-");

    QDialog dialog(parent);
    dialog.setWindowTitle("Job: " + ns + "/" + name);
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
    resourceForm->addRow("Completions", new QLabel(completions.isDouble() ? QString::number(completions.toInt()) : "-"));
    resourceForm->addRow("Parallelism", new QLabel(parallelism.isDouble() ? QString::number(parallelism.toInt()) : "-"));
    QStringList images;
    for (const auto &containerValue: spec["template"].toObject()["spec"].toObject()["containers"].toArray()) {
        images << containerValue.toObject()["image"].toString();
    }
    auto *imagesLabel = new QLabel(images.join("\n"));
    imagesLabel->setWordWrap(true);
    resourceForm->addRow("Images", imagesLabel);
    layout->addWidget(resourceBox);

    auto *conditionsBox = new QGroupBox("Conditions");
    auto *conditionsLayout = new QVBoxLayout(conditionsBox);
    auto *conditionsTable =
            KubeFormat::makeDetailTable({"Type", "Status", "Last Probe", "Last Transition", "Reason", "Message"});
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

    auto *podsStatusBox = new QGroupBox("Pods status");
    auto *podsStatusForm = new QFormLayout(podsStatusBox);
    podsStatusForm->addRow("Running", new QLabel(QString::number(status["active"].toInt())));
    podsStatusForm->addRow("Desired", new QLabel(desired));
    layout->addWidget(podsStatusBox);

    auto *podsBox = new QGroupBox(QString("Pods (%1)").arg(pods.size()));
    auto *podsLayout = new QVBoxLayout(podsBox);
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
    layout->addWidget(podsBox);

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
