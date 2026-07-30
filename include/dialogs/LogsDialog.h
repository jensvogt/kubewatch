#pragma once

#include <QString>

class QWidget;

class LogsDialog {
public:
    // Shows logs for a pod, or (for a job) the pod(s) belonging to that job.
    static void Show(QWidget *parent, const QString &context, const QString &resource, const QString &name,
                      const QString &ns);
};
