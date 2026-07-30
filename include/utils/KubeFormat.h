#pragma once

// Qt includes
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTableWidget>

#include <utility>

namespace KubeFormat {
    QString computeAge(const QString &creationTimestamp);

    QString formatCreated(const QString &creationTimestamp);

    // Parses a Kubernetes CPU quantity ("500m" or "2") into millicores.
    qint64 parseCpuMillis(const QString &value);

    QString formatCpuMillis(qint64 millis);

    // Parses a Kubernetes memory quantity ("128Mi", "2Gi", "512k", "1288490188800m"
    // milli-bytes, plain bytes) into bytes.
    qint64 parseMemoryBytes(const QString &value);

    QString formatMemoryGiB(qint64 bytes);

    // Formats a byte count using whichever of Ki/Mi/Gi keeps it readable (e.g. "256 Mi",
    // "1.2 Gi") -- unlike formatMemoryGiB, which always uses Gi and so renders small,
    // container-scale quantities as an unreadable "0.2 Gi".
    QString formatMemoryBytes(qint64 bytes);

    // Succeeded/Failed pods linger in the API (e.g. completed Job/CronJob pods) without
    // holding any node resources. kubectl describe node's "Non-terminated Pods" summary
    // (and the Kubernetes Dashboard's node Allocation section) exclude them for exactly
    // this reason -- otherwise counts/CPU/memory sums come out inflated.
    bool isTerminatedPodPhase(const QString &phase);

    QString joinKeyValues(const QJsonObject &obj);

    QString formatResourceList(const QJsonObject &resourceMap);

    QString formatProbeEndpoint(const QJsonObject &probe);

    QJsonObject findByName(const QJsonArray &array, const QString &name);

    std::pair<QString, QString> volumeSourceInfo(const QJsonObject &volume);

    QTableWidget *makeDetailTable(const QStringList &headers);
} // namespace KubeFormat
