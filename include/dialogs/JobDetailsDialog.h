#pragma once

#include <QString>
#include <QStringList>

class QWidget;

class JobDetailsDialog {
public:
    static void Show(QWidget *parent, const QStringList &baseArgs, const QString &name, const QString &ns);
};
