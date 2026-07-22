
#include <utils/Configuration.h>
#include <utils/Logging.h>

void Configuration::SetFilePath(const QString &filePath) {
    _filePath = filePath;
    ReadConfigurationFile(filePath);
}

void Configuration::ReadConfigurationFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logError << "Failed to open config file:" << file.errorString();
        return;
    }

    const QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        logError << "JSON parse error:" << parseError.errorString();
        return;
    }

    if (!doc.isObject()) {
        logError << "Invalid JSON format: root is not an object";
        return;
    }

    // Root
    _configurationRoot = doc.object();
}

void Configuration::WriteConfigurationFile(const QString &filePath) {
    if (!filePath.isEmpty()) {
        this->_filePath = filePath;
    }

    // Wrap it in a QJsonDocument
    const QJsonDocument doc(_configurationRoot);

    // Open the file for writing
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logWarning << "Couldn't open file for writing, error: " << file.errorString() << ", file: " << file.fileName();
        return;
    }

    // Write formatted (pretty-printed) JSON
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}
