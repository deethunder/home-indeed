#pragma once
#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QGroupBox>

class HomeInSettingsTab : public QWidget {
    Q_OBJECT
public:
    explicit HomeInSettingsTab(QWidget *parent = nullptr);

    // Getters so the Main Dock can read settings when pushing text
    bool AutoSwitchTabs() const;
    bool AutoSearch() const;
    bool AutoPush() const;
    QString GetDeepgramKey() const;
    int GetLinesPerPage() const;

signals:
    // Shouts when the audio source changes so the Audio Handler can restart
    void AudioSourceChanged(const QString& source_name);

private:
    QComboBox* audio_source_combo;
    QComboBox* align_combo;
    QComboBox* font_color_combo;
    QComboBox* lines_per_page_combo;
    QCheckBox* auto_switch_tabs_checkbox;
    QCheckBox* auto_search_checkbox;
    QCheckBox* auto_push_checkbox;
    QLineEdit* deepgram_key_edit;

    void LoadSettings();

private slots:
    void SaveSettings();
};