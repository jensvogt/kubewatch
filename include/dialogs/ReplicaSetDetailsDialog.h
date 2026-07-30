#pragma once

#include <QString>

class QWidget;

class ReplicaSetDetailsDialog {
public:
    static void Show(QWidget *parent, const QString &context, const QString &name, const QString &ns);
};
