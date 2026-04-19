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
#include "../stt/homein-stt.hpp"
#include "../database/homein-db.hpp"
#include "../database/homein-lyrics-db.hpp"
#include "../detection/homein-ref-parser.hpp"
#include "../detection/homein-lyrics-engine.hpp"
#include "../updater/homein-updater.hpp"

/**
 * @brief The primary control dock for Home Indeed.
 * Provides live transcription view and Bible/Song tabs.
 */
class HomeInDock : public QWidget {
    Q_OBJECT

public:
    explicit HomeInDock(QWidget *parent = nullptr);
    ~HomeInDock();

    /**
     * @brief Updates the transcript area with new text.
     */
    Q_INVOKABLE void AppendTranscript(const std::string& text);

    /**
     * @brief Displays a scripture suggestion.
     */
    Q_INVOKABLE void ShowBibleSuggestion(const std::string& book, int chapter, int verse, const std::string& text);

    /**
     * @brief Displays lyrics results.
     */
    Q_INVOKABLE void ShowLyricsResults(const std::vector<SongLyric>& results);

private:
    void SetupUI();
    void StartTranscription();
    void CheckForReferences(const std::string& text);
    void PerformFuzzySearch(const std::string& text);
    void SearchLyrics(const std::string& query);
    void CheckForLyrics(const std::string& text);

    QTextEdit *transcript_view;
    QTextEdit *bible_suggestion_view;
    QLineEdit *bible_search_input;
    QLabel *suggestion_label;
    QPushButton *push_btn;

    // Lyrics Tab UI elements
    QLineEdit *lyrics_search_input;
    QTextEdit *lyrics_result_view;
    QCheckBox *allow_web_checkbox;
    QPushButton *prev_verse_btn;
    QPushButton *next_verse_btn;
    
    // Display Settings
    QComboBox *align_combo;
    QCheckBox *fullscreen_checkbox;
    
    std::vector<std::string> current_song_lines;
    int current_verse_index = -1;

    HomeInSTTEngine stt_engine;
    HomeInDB bible_db;
    HomeInRefParser ref_parser;
    HomeInLyricsEngine lyrics_engine;
    HomeInUpdateChecker updater;
};
