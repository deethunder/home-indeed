#include "homein-updater.hpp"
#include "network/homein-http.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent/QtConcurrent>
#include <obs-module.h>

extern "C" const char* obs_module_get_version_string(void);

HomeInUpdateChecker::HomeInUpdateChecker(QObject *parent) 
    : QObject(parent) {
    // In a real OBS plugin, we'd pull this from the build system
    current_version = "v1.0.0"; 
}

void HomeInUpdateChecker::CheckForUpdates(const QString& repo_path) {
    QtConcurrent::run([this, repo_path]() {
        std::string url = "https://api.github.com/repos/" + repo_path.toStdString() + "/releases/latest";
        std::string response = HomeIn::HttpClient::Get(url);

        if (!response.empty()) {
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(response));
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject obj = doc.object();
                QString latest_version = obj["tag_name"].toString();
                QString notes = obj["body"].toString();
                QString download_url = obj["html_url"].toString();

                if (!latest_version.isEmpty() && latest_version != current_version) {
                    QMetaObject::invokeMethod(this, [this, latest_version, notes, download_url]() {
                        emit UpdateAvailable(latest_version, notes, download_url);
                    }, Qt::QueuedConnection);
                }
            }
        }
    });
}

