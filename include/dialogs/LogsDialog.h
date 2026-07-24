#pragma once

#include <QString>
#include <QStringList>

class QWidget;

class LogsDialog {
public:
    // Shows logs for a pod, or (for a job) the pod(s) belonging to that job.
    static void Show(QWidget *parent, const QStringList &baseArgs, const QString &resource, const QString &name,
                      const QString &ns);
};
