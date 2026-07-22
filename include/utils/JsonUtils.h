#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <QJsonDocument>
#include <QJsonObject>

class JsonUtils {
public:
    /**
     * @brief Write a JSON object to qDebug()
     *
     * @param obj JSON object
     */
    static void WriteJsonString(const QJsonObject &obj) {
        // Convert to JSON document
        const QJsonDocument doc(obj);

        // Convert to compact string
        const QString jsonString = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

        qDebug().noquote() << jsonString;
    }

    static void WriteJsonString(const QJsonArray &array) {
        // Convert to JSON document
        const QJsonDocument doc(array);

        // Convert to compact string
        const QString jsonString = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

        qDebug() << jsonString;
    }

    static QString WriteJsonToString(const QJsonObject &object) {
        // Convert to JSON document
        const QJsonDocument doc(object);

        // Convert to compact string
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    /**
     * @brief Return a json value by path
     *
     * @param root root JSON object
     * @param path path to value in dot notation, i.e.: a.b.c[2].d
     * @return JSON value
     */
    static QJsonValue JsonValueByPath(const QJsonValue &root, const QString &path) {
        QStringList tokens;
        QString token;
        bool inBracket = false;

        // --- Parse path: split into tokens like ["a","b","2","c"] ---
        for (int i = 0; i < path.length(); ++i) {
            QChar c = path[i];

            if (c == '.') {
                if (!inBracket) {
                    if (!token.isEmpty()) tokens << token;
                    token.clear();
                    continue;
                }
            }

            if (c == '[') {
                if (!token.isEmpty()) {
                    tokens << token;
                    token.clear();
                }
                inBracket = true;
                continue;
            }

            if (c == ']') {
                if (inBracket) {
                    if (!token.isEmpty()) tokens << token;
                    token.clear();
                    inBracket = false;
                }
                continue;
            }

            token.append(c);
        }

        if (!token.isEmpty())
            tokens << token;

        // --- Navigate JSON ---
        QJsonValue current = root;

        for (const QString &t: tokens) {

            if (current.isObject()) {
                QJsonObject obj = current.toObject();
                if (!obj.contains(t))
                    return {}; // key not found
                current = obj.value(t);
            } else if (current.isArray()) {
                bool ok = false;
                const int index = t.toInt(&ok);
                if (!ok)
                    return {}; // invalid array index

                QJsonArray arr = current.toArray();
                if (index < 0 || index >= arr.size())
                    return {}; // out of range

                current = arr[index];
            } else {
                return {}; // cannot navigate further
            }
        }

        return current;
    }

    struct PathElement {
        QString key;
        int index = -1; // -1 = no array index
    };

    // Split path like "a.b[2].c" into elements
    static QStringList splitPath(const QString &path) {
        QStringList parts;
        static const QRegularExpression re(R"(\.?([a-zA-Z0-9_-]+)(\[\d*\])?)"); // \d* allows empty []

        QRegularExpressionMatchIterator i = re.globalMatch(path);
        while (i.hasNext()) {
            const QRegularExpressionMatch match = i.next();
            QString part = match.captured(1);
            if (const QString index = match.captured(2); !index.isEmpty())
                part += index;
            parts.append(part);
        }
        return parts;
    }

    // Recursive helper
    static void setByPath(QJsonObject &obj, const QStringList &pathParts, const QJsonValue &value, const int depth = 0) {
        if (depth >= pathParts.size()) return;

        static const QRegularExpression re(R"(^([a-zA-Z0-9_-]+)\[(\d*)\]$)"); // \d* allows empty for append

        const QString &key = pathParts[depth];
        const bool isFinal = (depth == pathParts.size() - 1);

        if (const QRegularExpressionMatch match = re.match(key); match.hasMatch()) {
            const QString arrayKey = match.captured(1);
            const QString idxStr = match.captured(2);

            QJsonArray arr = obj[arrayKey].toArray();

            // Empty brackets = append, otherwise use given index
            const bool isAppend = idxStr.isEmpty();
            const int idx = isAppend ? static_cast<int>(arr.size()) : idxStr.toInt();

            // Sanity check
            if (idx < 0 || idx > 10000) return;

            // Grow array if needed
            while (arr.size() <= idx)
                arr.append(QJsonValue());

            if (isFinal) {
                arr[idx] = value;
            } else {
                QJsonObject nextObj = arr[idx].toObject();
                setByPath(nextObj, pathParts, value, depth + 1);
                arr[idx] = nextObj;
            }

            obj[arrayKey] = arr;

        } else {
            // Plain object key
            if (isFinal) {
                obj[key] = value;
            } else {
                QJsonObject child = obj[key].toObject();
                setByPath(child, pathParts, value, depth + 1);
                obj[key] = child;
            }
        }
    }

    // Public interface
    static void setByPath(QJsonObject &obj, const QString &path, const QJsonValue &value) {
        setByPath(obj, splitPath(path), value);
    }

    static QString PrettyPrint(const QString &rawJson) {
        QJsonParseError error;

        // Parse the raw string into a QJsonDocument
        const QJsonDocument doc = QJsonDocument::fromJson(rawJson.toUtf8(), &error);

        if (error.error != QJsonParseError::NoError) {
            return "Invalid JSON: " + error.errorString();
        }

        // Convert back to QString using the Indented format
        return doc.toJson(QJsonDocument::Indented);
    }
};

#endif // JSON_UTILS_H
