#pragma once

#include <QString>
#include <QStringList>

class QWidget;

class NamespaceDetailsDialog {
public:
    static void Show(QWidget *parent, const QStringList &baseArgs, const QString &name);
};
