//
// Created by vogje01 on 11/6/25.
//

#ifndef AWSMOCK_QT_UI_ICON_UTILS_H
#define AWSMOCK_QT_UI_ICON_UTILS_H
#include <qpixmap.h>

#include <QIcon>
#include <QStyle>
#include <QApplication>

#include <utils/Configuration.h>

class IconUtils {
public:
    IconUtils() = delete;

    static QIcon GetIcon(const QString &name);

    static QIcon GetIcon(const QString &style, const QString &name);

    static QIcon GetPngIcon(const QString &style, const QString &name);

    static QIcon GetPngIcon(const QString &name);

    static QIcon GetCommonIcon(const QString &name);
};

#endif //AWSMOCK_QT_UI_ICON_UTILS_H
