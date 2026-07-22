//
// Created by vogje01 on 11/19/25.
//

#pragma once

#include <QObject>

class EventBus final : public QObject {
    Q_OBJECT

public:
    static EventBus &instance() {
        static EventBus b;
        return b;
    }

signals:
    void TimerSignal(const QString &name, qint64 elapsed);

    void DockerStatsTimerSignal(const QString &name, qint64 elapsed);

    void RouteChanged(const QString &name, const QMap<QString, QString> &arguments = {});

    void MainStatusSignal(const QString &message);

    void PingSignal(bool result);

    void LogSignal(int logLevel, const QString &message);

    void FtpUploadSignal(const QString &path);
};
