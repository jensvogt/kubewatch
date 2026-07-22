//
// Created by vogje01 on 12/20/25.
//

#pragma once

// Qt includes
#include <QLocale>
#include <QString>

// Awsmock includes
#include "Configuration.h"

class DateTimeUtils {
public:
    static QString GetDateTimeFormat(const QDateTime &dateTime) {
        if (!Configuration::instance().GetValue<QString>("ui.datetime-format").isEmpty()) {
            return dateTime.toString(Configuration::instance().GetValue<QString>("ui.datetime-format"));
        }
        if (const auto locale = Configuration::instance().GetValue<QString>("ui.default-locale"); locale == "US") {
            const QLocale us(QLocale::English, QLocale::UnitedStates);
            return us.toString(dateTime, QLocale::ShortFormat);
        } else {
            if (locale == "UK") {
                const QLocale uk(QLocale::English, QLocale::UnitedKingdom);
                return uk.toString(dateTime, QLocale::ShortFormat);
            }
            if (locale == "DE") {
                const QLocale de(QLocale::German, QLocale::Germany);
                return de.toString(dateTime, QLocale::ShortFormat);
            }
        }
        return "Unknown locale";
    }

    static QString GetDateTimeFormat(const std::optional<QDateTime> &dateTime) {
        if (!dateTime.has_value()) {
            return "-";
        }
        return GetDateTimeFormat(*dateTime);
    }

    static QString GetLogTimeFormat(const QDateTime &dateTime) {
        if (!Configuration::instance().GetValue<QString>("ui.time-format").isEmpty()) {
            return dateTime.toString(Configuration::instance().GetValue<QString>("ui.time-format"));
        }
        if (const auto locale = Configuration::instance().GetValue<QString>("ui.default-locale"); locale == "US") {
            const QLocale us(QLocale::English, QLocale::UnitedStates);
            return us.toString(dateTime, QLocale::ShortFormat);
        } else {
            if (locale == "UK") {
                const QLocale uk(QLocale::English, QLocale::UnitedKingdom);
                return uk.toString(dateTime, QLocale::ShortFormat);
            }
            if (locale == "DE") {
                const QLocale de(QLocale::German, QLocale::Germany);
                return de.toString(dateTime, QLocale::ShortFormat);
            }
        }
        return "Unknown locale";
    }

    static QString GetLogDateTimeFormat(const QDateTime &dateTime) {
        if (!Configuration::instance().GetValue<QString>("ui.datetime-format").isEmpty()) {
            return dateTime.toString(Configuration::instance().GetValue<QString>("ui.datetime-format") + ".zzz");
        }
        if (const auto locale = Configuration::instance().GetValue<QString>("ui.datetime-locale"); locale == "US") {
            const QLocale us(QLocale::English, QLocale::UnitedStates);
            return us.toString(dateTime, QLocale::ShortFormat);
        } else {
            if (locale == "UK") {
                const QLocale uk(QLocale::English, QLocale::UnitedKingdom);
                return uk.toString(dateTime, QLocale::ShortFormat);
            }
            if (locale == "DE") {
                const QLocale de(QLocale::German, QLocale::Germany);
                return de.toString(dateTime, QLocale::ShortFormat);
            }
        }
        return "Unknown locale";
    }

    static QDateTime GetLastMidnight() {
        QDateTime startOfDay = QDateTime::currentDateTime();
        startOfDay.setTime(QTime(0, 0, 0));
        return startOfDay;
    }
};
