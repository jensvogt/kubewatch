#pragma once

#include <QString>
#include <QStringList>

class QWidget;

class EditResourceDialog {
public:
    // Fetches the resource's YAML, lets the user edit and save it (applying via
    // kubectl on Save), and returns whether the dialog was accepted -- so the
    // caller knows whether to refresh the page behind it.
    static bool Show(QWidget *parent, const QStringList &baseArgs, const QString &resource, const QString &name,
                      const QString &ns);
};
