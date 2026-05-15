#pragma once
#include <obs-frontend-api.h>
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
#include <QGridLayout>

// Core STT Interface and Universal Manager
#include "../stt/ISTTProvider.hpp"
#include "../stt/homein-transcript-queue.hpp"
#include "../stt/homein-stt.hpp"

#include "../detection/homein-context.hpp"
#include "../database/homein-db.hpp"
#include "../database/homein-lyrics-db.hpp"
#include "../detection/homein-ref-parser.hpp"
#include "../detection/homein-lyrics-engine.hpp"
#include "../updater/homein-updater.hpp"
#include <unordered_set>

class HomeInDock : public QWidget {
    Q_OBJECT

public:
    explicit HomeInDock(QWidget *parent = nullptr);
    ~HomeInDock();

    Q_INVOKABLE void AppendTranscript(const std::string& text);
    Q_INVOKABLE void ShowBibleSuggestion(const std::string& book, int chapter,
                                          int verse, const std::string& text);
    Q_INVOKABLE void ShowBibleVerseAtIndex(int index);
    Q_INVOKABLE void ShowLyricsResults(const std::vector<SongLyric>& results);
    Q_INVOKABLE void OnSongSelected(QListWidgetItem* item);

    enum class FocusMode { Auto, Bible, Songs };

private slots:
    void AddToQueue();
    void RemoveFromQueue();
    void ToggleSettings();
    void MoveQueueUp();
    void MoveQueueDown();
    void UpdateOverlayFromSelection();
    void UpdateAudioTest();
    void OnManualEntry();
    void OnToggleMic();
    void OnTogglePause();
    void OnImportEasyWorship();
    void OnShowHelp();
    void OnBibleSearchRequested();
    void OnLyricsSearchChanged(const QString& text);
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
    void ClearBibleGrid();
    void PopulateChapterGrid(const std::string& book_name, int count);
    void PopulateBookGrid();
    void PopulateAudioSources();
    void ApplySavedAudioSource();
    static void OBSFrontendEventCallback(enum obs_frontend_event event, void *private_data);
    void OnChapterSelected();
    void OnBookSelected();
    void ApplySettings();
    void RefreshBibleView();
    void SaveQueue();
    void LoadQueue();

    std::string CurrentTranslation() const;

    FocusMode current_focus = FocusMode::Auto;
    bool mic_active  = false;
    bool mic_paused  = false;
    bool is_searching = false;
    bool is_transcribing = false;

    QPushButton    *mic_btn;
    QPushButton    *pause_btn;
    QComboBox      *focus_combo;
    QStackedWidget *view_stack;
    QWidget        *tabs_page;
    QWidget        *settings_page;
    QTabWidget     *tabs_widget;

    QTextEdit  *transcript_view;
    QLineEdit  *bible_search_input;
    QLabel     *suggestion_label;
    QLabel     *lyrics_suggestion_label;
    QPushButton *push_btn;
    QPushButton *bible_prev_btn;
    QPushButton *bible_next_btn;
    QWidget    *bible_grid_container;
    QGridLayout *bible_grid_layout;
    std::string current_search_book;
    std::vector<BibleVerse> current_chapter_verses;
    int current_bible_verse_index = -1;
    QListWidget *bible_verses_list;

    QLineEdit   *lyrics_search_input;
    QCheckBox   *allow_web_checkbox;
    QPushButton *prev_verse_btn;
    QPushButton *next_verse_btn;
    QPushButton *lyrics_push_btn;
    QPushButton *lyrics_clear_btn;
    QListWidget *lyrics_results_list;
    QListWidget *lyrics_verses_list;
    QListWidget *queue_list;
    QListWidget *queue_breakdown_list;
    QPushButton *push_queue_btn;
    QPushButton *clear_queue_btn;
    QPushButton *up_queue_btn;
    QPushButton *down_queue_btn;

    QComboBox  *align_combo;
    QComboBox  *font_color_combo;
    QComboBox  *audio_source_combo;
    QCheckBox  *fullscreen_checkbox;
    QComboBox  *bible_version_combo;
    QComboBox  *lines_per_page_combo;
    QCheckBox  *auto_switch_tabs_checkbox;
    QCheckBox  *auto_search_checkbox;
    QCheckBox  *auto_push_checkbox;
    QLineEdit  *deepgram_key_edit;
    bool auto_switch_tabs = true;
    bool auto_search      = true;
    bool auto_push        = true;

    QProgressBar *audio_level_bar;
    QLineEdit    *last_word_field;
    QLabel       *web_icon_label;
    QTimer       *level_timer;
    std::vector<std::string> current_song_lines;
    int current_verse_index = -1;
    int lines_per_page = 2;

    void DetectionLoop();
    void RunDetection(const std::string& text);

    std::unique_ptr<ISTTProvider> stt_provider;
    std::shared_ptr<TranscriptQueue> transcript_queue;
    std::shared_ptr<SermonContext> sermon_context;
    std::thread detection_thread;
    std::atomic<bool> detection_running{false};

    HomeInRefParser      ref_parser;
    HomeInDB             bible_db;
    HomeInLyricsEngine   lyrics_engine;
    HomeInUpdateChecker  updater;

    SongLyric current_song;
    std::unordered_set<std::string> queued_refs;
    QTimer *lyrics_debounce;
};