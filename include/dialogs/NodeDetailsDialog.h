#pragma once

#include <QString>

class QWidget;

class NodeDetailsDialog {
public:
    static void Show(QWidget *parent, const QString &context, const QString &name);
};
