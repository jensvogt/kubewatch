#pragma once

#include <QString>
#include <QStringList>

class QWidget;

class NodeDetailsDialog {
public:
    static void Show(QWidget *parent, const QStringList &baseArgs, const QString &name);
};
