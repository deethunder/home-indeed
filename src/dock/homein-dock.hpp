#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <memory>
#include <QTabWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QStackedWidget>
#include <QListWidget>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QLineEdit>
#include <QTimer>
#include "../stt/homein-stt.hpp"
#include "../database/homein-db.hpp"
#include "../database/homein-lyrics-db.hpp"
#include "../detection/homein-ref-parser.hpp"
#include "../detection/homein-lyrics-engine.hpp"
#include "../updater/homein-updater.hpp"

/**
 * @brief The primary control dock for Home Indeed.
 * Now featuring a swappable view stack and a native OBS-style toolbar.
 */
class HomeInDock : public QWidget {
    Q_OBJECT

public:
    explicit HomeInDock(QWidget *parent = nullptr);
    ~HomeInDock();

    Q_INVOKABLE void AppendTranscript(const std::string& text);
    Q_INVOKABLE void ShowBibleSuggestion(const std::string& book, int chapter, int verse, const std::string& text);
    Q_INVOKABLE void ShowLyricsResults(const std::vector<SongLyric>& results);

    enum class FocusMode { Auto, Bible, Songs };

private slots:
    void AddToQueue();
    void RemoveFromQueue();
    void ToggleSettings();
    void MoveQueueUp();
    void MoveQueueDown();
    void UpdateOverlayFromSelection();
    void UpdateAudioTest();
    void OnToggleMic();
    void OnTogglePause();
    void OnImportEasyWorship();
    void SetFocusMode(FocusMode mode);

private:
    void SetupUI();
    void SetupToolbar(QVBoxLayout *main_layout);
    void SetupSettingsView(QWidget *parent);
    void StartTranscription();
    void StopTranscription();
    void CheckForReferences(const std::string& text);
    void PerformFuzzySearch(const std::string& text);
    void SearchLyrics(const std::string& query);
    void CheckForLyrics(const std::string& text);
    void PopulateTranslations();

    FocusMode current_focus = FocusMode::Auto;
    bool mic_active = false;
    bool mic_paused = false;

    // UI Buttons for Mic Hub
    QPushButton *mic_btn;
    QPushButton *pause_btn;
    QComboBox *focus_combo;

    // View stack for swapping between Tabs and Settings
    QStackedWidget *view_stack;
    QWidget *tabs_page;
    QWidget *settings_page;

    // Tabs
    QTabWidget *tabs_widget;
    
    // Bible suggest
    QTextEdit *transcript_view;
    QTextEdit *bible_suggestion_view;
    QLineEdit *bible_search_input;
    QLabel *suggestion_label;
    QPushButton *push_btn;

    // Lyrics
    QLineEdit *lyrics_search_input;
    QTextEdit *lyrics_result_view;
    QCheckBox *allow_web_checkbox;
    QPushButton *prev_verse_btn;
    QPushButton *next_verse_btn;

    // Queue
    QListWidget *queue_list;
    
    // Display Settings
    QComboBox *align_combo;
    QCheckBox *fullscreen_checkbox;
    QComboBox *bible_version_combo;

    // Automation Settings
    QCheckBox *auto_switch_tabs_checkbox;
    QCheckBox *auto_search_checkbox;
    QCheckBox *auto_push_checkbox;
    bool auto_switch_tabs = true;
    bool auto_search = true;
    bool auto_push = true;

    // Testing UI
    QProgressBar *audio_level_bar;
    QLineEdit *last_word_field;
    QTimer *level_timer;
    
    std::vector<std::string> current_song_lines;
    int current_verse_index = -1;

    HomeInSTTEngine stt_engine;
    HomeInDB bible_db;
    HomeInRefParser ref_parser;
    HomeInLyricsEngine lyrics_engine;
    HomeInUpdateChecker updater;
};
