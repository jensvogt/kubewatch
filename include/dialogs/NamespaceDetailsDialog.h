#pragma once

#include <QString>

class QWidget;

class NamespaceDetailsDialog {
public:
    static void Show(QWidget *parent, const QString &context, const QString &name);
};
