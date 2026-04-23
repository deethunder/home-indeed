#pragma once

#include <QObject>
#include <QString>

/**
 * @class HomeInUpdateChecker
 * @brief Asynchronous update checker for the Home Indeed plugin.
 * 
 * Hits the GitHub Releases API to find the latest tag and compares it
 * with the current plugin version. Emits a signal if a newer version exists.
 */
class HomeInUpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit HomeInUpdateChecker(QObject *parent = nullptr);

    /**
     * @brief Starts an asynchronous check against GitHub.
     * @param repo_path The GitHub repo path (e.g., "DeeThunder/Home-Indeed").
     */
    void CheckForUpdates(const QString& repo_path);

signals:
    /**
     * @brief Emitted when a newer version is found.
     * @param new_version The string tag of the new version.
     * @param release_notes Brief summary of the release.
     * @param download_url Link to the release page.
     */
    void UpdateAvailable(const QString& new_version, const QString& release_notes, const QString& download_url);

private:
    QString current_version;
};
