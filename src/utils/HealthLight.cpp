#include <utils/HealthLight.h>

#include <QColor>
#include <QPainter>
#include <QPixmap>

namespace {

    QIcon StatusLight(const QColor &color) {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(2, 2, 12, 12);
        return QIcon(pixmap);
    }

}// namespace

QIcon HealthLight::Icon(const Health health) {
    switch (health) {
        case Health::Error:
            return StatusLight(QColor(0xE5, 0x39, 0x35));// red
        case Health::Warning:
            return StatusLight(QColor(0xFB, 0x8C, 0x00));// orange
        default:
            return StatusLight(QColor(0x43, 0xA0, 0x47));// green
    }
}
