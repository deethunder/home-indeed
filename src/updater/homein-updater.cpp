#include "homein-updater.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <obs-module.h>

extern "C" const char* obs_module_get_version_string(void);

HomeInUpdateChecker::HomeInUpdateChecker(QObject *parent) 
    : QObject(parent), network_manager(new QNetworkAccessManager(this)) {
    // In a real OBS plugin, we'd pull this from the build system
    current_version = "v1.0.0"; 
}

void HomeInUpdateChecker::CheckForUpdates(const QString& repo_path) {
    QString url = QString("https://api.github.com/repos/%1/releases/latest").arg(repo_path);
    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Home-Indeed-OBS-Plugin/1.0");

    connect(network_manager, &QNetworkAccessManager::finished, this, &HomeInUpdateChecker::OnReplyFinished);
    network_manager->get(request);
}

void HomeInUpdateChecker::OnReplyFinished(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        blog(LOG_WARNING, "Update check failed: %s", reply->errorString().toStdString().c_str());
        reply->deleteLater();
        return;
    }

    QByteArray response = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (!doc.isNull() && doc.isObject()) {
        QJsonObject obj = doc.object();
        QString latest_version = obj["tag_name"].toString();
        QString notes = obj["body"].toString();
        QString download_url = obj["html_url"].toString();

        // Simple comparison: if the strings are different, we assume an update
        // (A more robust version would parse semantic versioning)
        if (!latest_version.isEmpty() && latest_version != current_version) {
            emit UpdateAvailable(latest_version, notes, download_url);
        }
    }

    reply->deleteLater();
}
