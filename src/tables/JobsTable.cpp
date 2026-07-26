#include <tables/JobsTable.h>

#include <kubectl/KubectlClient.h>
#include <utils/EventBus.h>
#include <utils/HealthLight.h>
#include <utils/KubeFormat.h>

#include <QElapsedTimer>

using HealthLight::Health;

namespace {
    QString HealthTooltip(const Health health, const QString &jobStatus) {
        if (jobStatus == "Failed") return "Job failed";
        if (jobStatus == "Suspended") return "Job is suspended";
        if (jobStatus == "Complete") return "Completed successfully";
        return health == Health::Warning ? "Not all pods are running" : "Running as expected";
    }
}// namespace

JobsTable::JobsTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Status", "Pods", "Created", "Age", "Health", "Namespace"});
    SetHiddenColumns({kNamespaceColumn});
    // Keep "Name" as logical column 0 (row selection and context-menu actions rely
    // on it), but display Health as the leftmost column.
    MoveColumnToFront(kHealthColumn);
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
        const int requestedCount = completions.isDouble()
                                        ? completions.toInt()
                                        : parallelism.isDouble()
                                              ? parallelism.toInt()
                                              : -1;
        const QString requested = requestedCount >= 0 ? QString::number(requestedCount) : QStringLiteral("-");
        const QString pods = QString("%1/%2").arg(running).arg(requested);

        Health health;
        if (jobStatus == "Failed") {
            health = Health::Error;
        } else if (jobStatus == "Complete") {
            // The job's pods have exited after completion, so 0 running is expected.
            health = Health::Ok;
        } else if (jobStatus == "Suspended") {
            health = Health::Warning;
        } else if (requestedCount >= 0 && running < requestedCount) {
            health = Health::Warning;
        } else {
            health = Health::Ok;
        }

        SetColumn(row, 0, metadata["name"].toString());
        SetColumn(row, 1, jobStatus);
        SetColumn(row, 2, pods, Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, 3, KubeFormat::formatCreated(metadata["creationTimestamp"].toString()));
        SetColumn(row, 4, KubeFormat::computeAge(metadata["creationTimestamp"].toString()), Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, kHealthColumn, HealthLight::Icon(health), static_cast<long>(health), HealthTooltip(health, jobStatus));
        SetHiddenColumn(row, kNamespaceColumn, metadata["namespace"].toString());
    });
    EventBus::instance().TimerSignal("jobs", timer.elapsed());
}
