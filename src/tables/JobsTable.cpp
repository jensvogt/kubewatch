#include <tables/JobsTable.h>

#include <kubectl/KubectlClient.h>
#include <utils/EventBus.h>
#include <utils/KubeFormat.h>

#include <QElapsedTimer>

JobsTable::JobsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Status", "Pods", "Created", "Age", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    setServiceApis({"jobs"});
}

void JobsTable::Refresh() {
    QElapsedTimer timer;
    timer.start();
    const QJsonArray items = KubectlClient::fetchItems(ResourceArgs("jobs"));

    PopulatePage(items, [&](int row, const QJsonObject &job) {
        const QJsonObject metadata = job["metadata"].toObject();
        const QJsonObject status = job["status"].toObject();
        const QJsonObject spec = job["spec"].toObject();

        QString jobStatus = "Running";
        for (const auto &conditionValue: status["conditions"].toArray()) {
            if (const QJsonObject condition = conditionValue.toObject(); condition["status"].toString() == "True") {
                jobStatus = condition["type"].toString();
            }
        }

        const int running = status["active"].toInt();
        const QJsonValue completions = spec["completions"];
        const QJsonValue parallelism = spec["parallelism"];
        const QString requested = completions.isDouble()
                                      ? QString::number(completions.toInt())
                                      : parallelism.isDouble()
                                            ? QString::number(parallelism.toInt())
                                            : QStringLiteral("-");
        const QString pods = QString("%1/%2").arg(running).arg(requested);

        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, jobStatus);
        SetColumn(row, 2, pods, Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, 3, KubeFormat::formatCreated(metadata["creationTimestamp"].toString()));
        SetColumn(row, 4, KubeFormat::computeAge(metadata["creationTimestamp"].toString()), Qt::AlignRight | Qt::AlignVCenter);
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("jobs", timer.elapsed());
}
