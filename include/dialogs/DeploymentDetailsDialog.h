#pragma once

#include <QString>

class QWidget;

class DeploymentDetailsDialog {
public:
    static void Show(QWidget *parent, const QString &context, const QString &name, const QString &ns);
};
