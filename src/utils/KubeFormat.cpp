#include <utils/KubeFormat.h>

#include <QAbstractItemView>
#include <QDateTime>
#include <QHeaderView>

namespace KubeFormat {
    QString computeAge(const QString &creationTimestamp) {
        const QDateTime created = QDateTime::fromString(creationTimestamp, Qt::ISODate);
        if (!created.isValid()) {
            return {};
        }
        const qint64 secs = created.secsTo(QDateTime::currentDateTimeUtc());
        if (secs < 60) return QString("%1s").arg(secs);
        if (secs < 3600) return QString("%1m").arg(secs / 60);
        if (secs < 86400) return QString("%1h").arg(secs / 3600);
        return QString("%1d").arg(secs / 86400);
    }

    QString formatCreated(const QString &creationTimestamp) {
        const QDateTime created = QDateTime::fromString(creationTimestamp, Qt::ISODate);
        if (!created.isValid()) {
            return {};
        }
        return created.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
    }

    qint64 parseCpuMillis(const QString &value) {
        if (value.isEmpty()) return 0;
        if (value.endsWith('m')) {
            return value.chopped(1).toLongLong();
        }
        bool ok = false;
        const double cores = value.toDouble(&ok);
        return ok ? static_cast<qint64>(cores * 1000) : 0;
    }

    QString formatCpuMillis(qint64 millis) {
        if (millis % 1000 == 0) {
            return QString::number(millis / 1000);
        }
        return QString("%1m").arg(millis);
    }

    qint64 parseMemoryBytes(const QString &value) {
        if (value.isEmpty()) return 0;
        static const QList<std::pair<QString, qint64>> suffixes = {
            {"Ki", 1024LL}, {"Mi", 1024LL * 1024}, {"Gi", 1024LL * 1024 * 1024}, {"Ti", 1024LL * 1024 * 1024 * 1024},
            {"K", 1000LL}, {"M", 1000LL * 1000}, {"G", 1000LL * 1000 * 1000}, {"T", 1000LL * 1000 * 1000 * 1000},
        };
        for (const auto &[suffix, factor]: suffixes) {
            if (value.endsWith(suffix)) {
                return static_cast<qint64>(value.chopped(suffix.size()).toDouble() * factor);
            }
        }
        bool ok = false;
        const qint64 bytes = value.toLongLong(&ok);
        return ok ? bytes : 0;
    }

    QString formatMemoryGiB(qint64 bytes) {
        return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " Gi";
    }

    bool isTerminatedPodPhase(const QString &phase) {
        return phase == "Succeeded" || phase == "Failed";
    }

    QString joinKeyValues(const QJsonObject &obj) {
        QStringList pairs;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            pairs << it.key() + ": " + it.value().toString();
        }
        return pairs.isEmpty() ? "-" : pairs.join("\n");
    }

    QString formatResourceList(const QJsonObject &resourceMap) {
        QStringList parts;
        for (auto it = resourceMap.begin(); it != resourceMap.end(); ++it) {
            parts << it.key() + ": " + it.value().toString();
        }
        return parts.isEmpty() ? "-" : parts.join(", ");
    }

    QString formatProbeEndpoint(const QJsonObject &probe) {
        if (probe.contains("httpGet")) {
            const QJsonObject httpGet = probe["httpGet"].toObject();
            const QString host = httpGet["host"].toString().isEmpty() ? "[Pod IP]" : httpGet["host"].toString();
            return QString("HTTP GET %1:%2%3").arg(host, httpGet["port"].toVariant().toString(), httpGet["path"].toString());
        }
        if (probe.contains("tcpSocket")) {
            return "TCP " + probe["tcpSocket"].toObject()["port"].toVariant().toString();
        }
        if (probe.contains("exec")) {
            QStringList command;
            for (const auto &value: probe["exec"].toObject()["command"].toArray()) {
                command << value.toString();
            }
            return "Exec: " + command.join(" ");
        }
        return "-";
    }

    QJsonObject findByName(const QJsonArray &array, const QString &name) {
        for (const auto &value: array) {
            if (const QJsonObject obj = value.toObject(); obj["name"].toString() == name) return obj;
        }
        return {};
    }

    std::pair<QString, QString> volumeSourceInfo(const QJsonObject &volume) {
        if (volume.contains("emptyDir")) return {"EmptyDir", "-"};
        if (volume.contains("secret")) return {"Secret", volume["secret"].toObject()["secretName"].toString()};
        if (volume.contains("configMap")) return {"ConfigMap", volume["configMap"].toObject()["name"].toString()};
        if (volume.contains("persistentVolumeClaim"))
            return {"PersistentVolumeClaim", volume["persistentVolumeClaim"].toObject()["claimName"].toString()};
        if (volume.contains("projected")) return {"Projected", "-"};
        if (volume.contains("hostPath")) return {"HostPath", volume["hostPath"].toObject()["path"].toString()};
        return {"-", "-"};
    }

    QTableWidget *makeDetailTable(const QStringList &headers) {
        auto *table = new QTableWidget();
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        return table;
    }
} // namespace KubeFormat
