#pragma once

// Qt includes
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVersionNumber>

// kubewatch includes
#include <Version.h>
#include <utils/Logging.h>

// Checks the version.txt published alongside the installers on GitHub Pages against
// the version this binary was built with, and emits UpdateAvailable() if a newer
// release exists.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr) : QObject(parent) {
    }

    void checkForUpdates() {
        request(true);
    }

    void checkForUpdatesNoNotification() {
        request(false);
    }

signals:
    // ver is the newer version string if one is available, or empty if the caller
    // asked to be notified either way and this build is already current.
    void UpdateAvailable(const QString &ver);

private:
    void request(bool notification) {
        const QUrl url("https://jensvogt.github.io/kubewatch/version.txt");
        const QNetworkRequest request(url);

        QNetworkReply *reply = manager_.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, notification]() {
            if (reply->error() == QNetworkReply::NoError) {
                const QString latestVersion = QString(reply->readAll()).trimmed();
                compareVersions(latestVersion, notification);
            }
            reply->deleteLater();
        });
    }

    void compareVersions(const QString &latest, const bool notification) {
        const QVersionNumber currentV = QVersionNumber::fromString(APP_VERSION);

        if (const QVersionNumber latestV = QVersionNumber::fromString(latest); currentV < latestV) {
            logInfo << "Update available! Current:" << currentV.toString() << "Latest:" << latest;
            emit UpdateAvailable(latest);
        } else if (notification) {
            logInfo << "You already have the latest version";
            emit UpdateAvailable({});
        }
    }

    QNetworkAccessManager manager_;
};
