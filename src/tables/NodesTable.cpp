#include <tables/NodesTable.h>

#include <kubectl/KubectlClient.h>
#include <utils/EventBus.h>
#include <utils/KubeFormat.h>

#include <QElapsedTimer>
#include <QHash>

NodesTable::NodesTable(QWidget *parent) : KubeTable(parent) {
    ConfigureHeaders({"Name", "Status", "Version", "Internal IP", "CPU Requests", "CPU Limits", "CPU Capacity", "Pods", "Created"});
    setServiceApis({"nodes"});
}

void NodesTable::Refresh() {
    QElapsedTimer timer;
    timer.start();
    // Both kubectl calls below are gathered under one guard so the busy overlay
    // shows/hides at most once for the pair, instead of potentially flickering
    // between them (each kubectl call otherwise manages the overlay on its own).
    BusyGuard busyGuard;
    QStringList args = BaseArgs();
    args << "get" << "nodes" << "-o" << "json";
    const QJsonArray items = KubectlClient::fetchItems(args);

    QStringList podArgs = BaseArgs();
    podArgs << "get" << "pods" << "--all-namespaces" << "-o" << "json";
    QHash<QString, int> podCountByNode;
    QHash<QString, qint64> cpuRequestMillisByNode;
    QHash<QString, qint64> cpuLimitMillisByNode;
    for (const auto &podValue: KubectlClient::fetchItems(podArgs)) {
        const QJsonObject pod = podValue.toObject();
        const QString nodeName = pod["spec"].toObject()["nodeName"].toString();
        if (nodeName.isEmpty() || KubeFormat::isTerminatedPodPhase(pod["status"].toObject()["phase"].toString())) continue;

        ++podCountByNode[nodeName];
        for (const auto &containerValue: pod["spec"].toObject()["containers"].toArray()) {
            const QJsonObject resources = containerValue.toObject()["resources"].toObject();
            cpuRequestMillisByNode[nodeName] += KubeFormat::parseCpuMillis(resources["requests"].toObject()["cpu"].toString());
            cpuLimitMillisByNode[nodeName] += KubeFormat::parseCpuMillis(resources["limits"].toObject()["cpu"].toString());
        }
    }

    PopulatePage(items, [&](const int row, const QJsonObject &node) {
        const QJsonObject metadata = node["metadata"].toObject();
        const QString name = metadata["name"].toString();

        QString readyStatus = "Unknown";
        for (const auto &conditionValue: node["status"].toObject()["conditions"].toArray()) {
            if (const QJsonObject condition = conditionValue.toObject(); condition["type"].toString() == "Ready") {
                readyStatus = condition["status"].toString() == "True" ? "Ready" : "NotReady";
            }
        }

        const QString version = node["status"].toObject()["nodeInfo"].toObject()["kubeletVersion"].toString();

        QString internalIp;
        for (const auto &addressValue: node["status"].toObject()["addresses"].toArray()) {
            if (const QJsonObject address = addressValue.toObject(); address["type"].toString() == "InternalIP") {
                internalIp = address["address"].toString();
            }
        }

        const int allocatablePods = node["status"].toObject()["allocatable"].toObject()["pods"].toString().toInt();
        const QString pods = QString("%1/%2").arg(podCountByNode.value(name)).arg(allocatablePods);
        const QString cpuRequests = KubeFormat::formatCpuMillis(cpuRequestMillisByNode.value(name));
        const QString cpuLimits = KubeFormat::formatCpuMillis(cpuLimitMillisByNode.value(name));
        const QString cpuCapacity = node["status"].toObject()["capacity"].toObject()["cpu"].toString();

        SetColumn(row, 0, name);
        SetColumn(row, 1, readyStatus);
        SetColumn(row, 2, version);
        SetColumn(row, 3, internalIp);
        SetColumn(row, 4, cpuRequests, Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, 5, cpuLimits, Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, 6, cpuCapacity, Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, 7, pods, Qt::AlignRight | Qt::AlignVCenter);
        SetColumn(row, 8, KubeFormat::formatCreated(metadata["creationTimestamp"].toString()));
    });
    EventBus::instance().TimerSignal("nodes", timer.elapsed());
}
