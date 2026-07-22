#ifndef AWSMOCK_QT_UI_CONFIGURATION_H
#define AWSMOCK_QT_UI_CONFIGURATION_H

// C++ includes
#include <iostream>
#include <string>

// Qt includes
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

// Awsmock includes
#include <utils/JsonUtils.h>

#ifdef _WIN32
#define DEFAULT_CONFIGURATION_FILE_PATH QString("C:\\Program Files\\awsmock-qt-ui\\awsmock-qt-ui.json")
#else
#define DEFAULT_CONFIGURATION_FILE_PATH QString("/usr/local/awsmock-qt-ui/etc/awsmock-qt-ui.json")
#endif

class Configuration final : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     */
    Configuration() = default;

    /**
     * @brief Singleton instance
     *
     * @return
     */
    static Configuration &instance() {
        static Configuration instance;
        return instance;
    }

    template<class T>
    T GetValue(const QString &path) {
        const QJsonValue v = JsonUtils::JsonValueByPath(_configurationRoot, path);
        if constexpr (std::is_same_v<T, int>) {
            return static_cast<T>(v.toInt());
        } else if constexpr (std::is_same_v<T, long>) {
            return static_cast<T>(v.toInteger());
        } else if constexpr (std::is_same_v<T, double>) {
            return static_cast<T>(v.toDouble());
        } else if constexpr (std::is_same_v<T, QString>) {
            return static_cast<T>(v.toString());
        } else if constexpr (std::is_same_v<T, bool>) {
            return static_cast<T>(v.toBool());
        } else if constexpr (std::is_same_v<T, QJsonObject>) {
            return static_cast<T>(v.toObject());
        } else if constexpr (std::is_same_v<T, QJsonArray>) {
            return static_cast<T>(v.toArray());
        } else {
            return {};
        }
    }

    template<class T>
    T GetValue(const QString &path, T defaultValue) {
        const QJsonValue v = JsonUtils::JsonValueByPath(_configurationRoot, path);
        if (v.isNull()) {
            std::cerr << "Configuration path not found: " << path.toStdString() << ", file: " << _filePath.toStdString() << std::endl;
        }
        if constexpr (std::is_same_v<T, int>) {
            return v.isDouble() ? static_cast<T>(v.toInt()) : defaultValue;
        } else if constexpr (std::is_same_v<T, long>) {
            return v.isDouble() ? static_cast<T>(v.toInteger()) : defaultValue;
        } else if constexpr (std::is_same_v<T, double>) {
            return v.isDouble() ? static_cast<T>(v.toDouble()) : defaultValue;
        } else if constexpr (std::is_same_v<T, QString>) {
            return v.isString() ? static_cast<T>(v.toString()) : defaultValue;
        } else if constexpr (std::is_same_v<T, bool>) {
            return v.isBool() ? static_cast<T>(v.toBool()) : defaultValue;
        } else if constexpr (std::is_same_v<T, QJsonObject>) {
            return v.isObject() ? static_cast<T>(v.toObject()) : defaultValue;
        } else if constexpr (std::is_same_v<T, QJsonArray>) {
            return v.isArray() ? static_cast<T>(v.toArray()) : defaultValue;
        } else {
            return defaultValue;
        }
    }

    template<class T>
    void SetValue(const QString &path, T value) {
        if constexpr (std::is_same_v<T, long>) {
            JsonUtils::setByPath(_configurationRoot, path, static_cast<qint64>(value));
            WriteConfigurationFile(_filePath);
            emit ConfigurationChanged(path, QString::number(value));
        } else if constexpr (std::is_same_v<T, int>) {
            JsonUtils::setByPath(_configurationRoot, path, static_cast<qint32>(value));
            WriteConfigurationFile(_filePath);
            emit ConfigurationChanged(path, QString::number(value));
        } else {
            JsonUtils::setByPath(_configurationRoot, path, static_cast<T>(value));
            WriteConfigurationFile(_filePath);
            emit ConfigurationChanged(path, value);
        }
    }

    /**
     * @brief Write a JSON configuration file
     *
     * @param filePath absolute file path of the configuration file
     */
    void WriteConfigurationFile(const QString &filePath);

    /**
     * @brief Write a JSON configuration file
     *
     * @param filePath absolute file path of the configuration file
     */
    void ReadConfigurationFile(const QString &filePath);

    /**
     * @brief SetFilePath
     *
     * @param filePath absolute path to configuration file
     */
    void SetFilePath(const QString &filePath);

signals:
    /**
     * @brief Send when a preferences changed
     *
     * @param key preference key
     * @param value preference value
     */
    void ConfigurationChanged(const QString &key, const QString &value);

private:
    /**
     * @brief Configuration root
     */
    QJsonObject _configurationRoot{};

    /**
     * @brief File path
     */
    QString _filePath = DEFAULT_CONFIGURATION_FILE_PATH;

    /**
     * @brief Connection flag
     */
    bool _connected = true;
};

#endif // AWSMOCK_QT_UI_CONFIGURATION_H
