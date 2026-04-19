#include "homein-dock.hpp"
#include <obs-module.h>
#include <QScrollBar>
#include <QApplication>
#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "../renderer/homein-renderer.hpp"

HomeInDock::HomeInDock(QWidget *parent) : QWidget(parent) {
    // Initialize Database
    char *db_path = obs_module_file("data/homein-bible.db");
    if (db_path) {
        bible_db.Open(db_path);
        bfree(db_path);
    }

    char *lyrics_db_path = obs_module_file("data/homein-lyrics.db");
    if (lyrics_db_path) {
        lyrics_engine.Initialize(lyrics_db_path);
        bfree(lyrics_db_path);
    }

    SetupUI();
    StartTranscription();

    // Start Update Check (Phase 3: Automated Updates)
    connect(&updater, &HomeInUpdateChecker::UpdateAvailable, [this](const QString& version, const QString& notes, const QString& url) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Home Indeed Update Available");
        msgBox.setText(QString("A new version (%1) of Home Indeed is available!").arg(version));
        msgBox.setInformativeText("Would you like to visit the download page?");
        msgBox.setDetailedText(notes);
        msgBox.setStandardButtons(QMessageBox::Open | QMessageBox::Close);
        msgBox.setDefaultButton(QMessageBox::Open);
        
        if (msgBox.exec() == QMessageBox::Open) {
            QDesktopServices::openUrl(QUrl(url));
        }
    });
    updater.CheckForUpdates("DeeThunder/Home-Indeed");
}

HomeInDock::~HomeInDock() {
    stt_engine.Stop();
}

void HomeInDock::SetupUI() {
    QVBoxLayout *layout = new QVBoxLayout(this);
    
    QTabWidget *tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    // Tab 1: Live Transcript
    QWidget *transcript_tab = new QWidget();
    QVBoxLayout *t_layout = new QVBoxLayout(transcript_tab);
    
    transcript_view = new QTextEdit(this);
    transcript_view->setReadOnly(true);
    transcript_view->setStyleSheet("background-color: #1e1e1e; color: #ffffff;");
    t_layout->addWidget(transcript_view);
    
    tabs->addTab(transcript_tab, "Transcript");

    // Tab 2: Bible Suggestions
    QWidget *bible_tab = new QWidget();
    QVBoxLayout *b_layout = new QVBoxLayout(bible_tab);
    
    QHBoxLayout *search_layout = new QHBoxLayout();
    bible_search_input = new QLineEdit(this);
    bible_search_input->setPlaceholderText("Manual search (e.g. Genesis 1:1)...");
    search_layout->addWidget(bible_search_input);
    
    QPushButton *search_btn = new QPushButton("Search", this);
    search_layout->addWidget(search_btn);
    b_layout->addLayout(search_layout);
    
    suggestion_label = new QLabel("No verse detected yet...", this);
    b_layout->addWidget(suggestion_label);

    bible_suggestion_view = new QTextEdit(this);
    bible_suggestion_view->setReadOnly(true);
    bible_suggestion_view->setStyleSheet("background-color: #1e2a1e; color: #aaffaa; font-size: 14px;");
    b_layout->addWidget(bible_suggestion_view);

    push_btn = new QPushButton("Push to Screen", this);
    push_btn->setStyleSheet("background-color: #2a4a2a; color: white; font-weight: bold; height: 30px;");
    b_layout->addWidget(push_btn);

    connect(search_btn, &QPushButton::clicked, [this]() {
        CheckForReferences(bible_search_input->text().toStdString());
    });

    connect(push_btn, &QPushButton::clicked, [this]() {
        std::string text = bible_suggestion_view->toPlainText().toStdString();
        std::string ref = suggestion_label->text().toStdString();
        
        HomeInRenderer* renderer = GetActiveRenderer();
        if (renderer) {
            obs_log(LOG_INFO, "Pushing verse to screen: %s", ref.c_str());
            renderer->SetText(ref + "\n" + text);
        } else {
            obs_log(LOG_WARNING, "No active Home Indeed Overlay source found in the scene.");
        }
    });

    tabs->addTab(bible_tab, "Bible");

    // Tab 3: Lyrics
    QWidget *lyrics_tab = new QWidget();
    QVBoxLayout *l_layout = new QVBoxLayout(lyrics_tab);
    
    QHBoxLayout *l_search_layout = new QHBoxLayout();
    lyrics_search_input = new QLineEdit(this);
    lyrics_search_input->setPlaceholderText("Search for a worship song...");
    l_search_layout->addWidget(lyrics_search_input);
    
    QPushButton *l_search_btn = new QPushButton("Find Lyrics", this);
    l_search_layout->addWidget(l_search_btn);
    l_layout->addLayout(l_search_layout);
    
    allow_web_checkbox = new QCheckBox("Allow Web Search (LRCLIB)", this);
    allow_web_checkbox->setChecked(true);
    l_layout->addWidget(allow_web_checkbox);

    lyrics_result_view = new QTextEdit(this);
    lyrics_result_view->setReadOnly(true);
    lyrics_result_view->setStyleSheet("background-color: #1a1a2e; color: #e1e1ff;");
    l_layout->addWidget(lyrics_result_view);

    QHBoxLayout *stepper_layout = new QHBoxLayout();
    prev_verse_btn = new QPushButton("Previous", this);
    next_verse_btn = new QPushButton("Next", this);
    stepper_layout->addWidget(prev_verse_btn);
    stepper_layout->addWidget(next_verse_btn);
    l_layout->addLayout(stepper_layout);

    QPushButton *l_push_btn = new QPushButton("Push Lyrics to Screen", this);
    l_push_btn->setStyleSheet("background-color: #1a1a5e; color: white; font-weight: bold; height: 30px;");
    l_layout->addWidget(l_push_btn);

    connect(l_search_btn, &QPushButton::clicked, [this]() {
        SearchLyrics(lyrics_search_input->text().toStdString());
    });

    connect(l_push_btn, &QPushButton::clicked, [this]() {
        std::string lyrics = lyrics_result_view->toPlainText().toStdString();
        HomeInRenderer* renderer = GetActiveRenderer();
        if (renderer) renderer->SetText(lyrics);
    });

    connect(prev_verse_btn, &QPushButton::clicked, [this]() {
        if (current_verse_index > 0) {
            current_verse_index--;
            lyrics_result_view->setText(QString::fromStdString(current_song_lines[current_verse_index]));
        }
    });

    connect(next_verse_btn, &QPushButton::clicked, [this]() {
        if (current_verse_index < (int)current_song_lines.size() - 1) {
            current_verse_index++;
            lyrics_result_view->setText(QString::fromStdString(current_song_lines[current_verse_index]));
        }
    });

    tabs->addTab(lyrics_tab, "Lyrics");

    // Display Settings Block (Shared)
    QGroupBox *settings_group = new QGroupBox("Display Settings", this);
    QVBoxLayout *s_layout = new QVBoxLayout(settings_group);
    
    QHBoxLayout *align_layout = new QHBoxLayout();
    align_layout->addWidget(new QLabel("Alignment:", this));
    align_combo = new QComboBox(this);
    align_combo->addItem("Center", (int)Qt::AlignCenter);
    align_combo->addItem("Left", (int)Qt::AlignLeft);
    align_combo->addItem("Right", (int)Qt::AlignRight);
    align_layout->addWidget(align_combo);
    s_layout->addLayout(align_layout);

    fullscreen_checkbox = new QCheckBox("Full Screen Mode (vs Lower Third)", this);
    s_layout->addWidget(fullscreen_checkbox);

    layout->addWidget(settings_group);

    // Wire up the settings to the renderer
    auto update_renderer_settings = [this]() {
        OverlaySettings s;
        s.alignment = (Qt::Alignment)align_combo->currentData().toInt();
        s.full_screen = fullscreen_checkbox->isChecked();
        s.font_family = "Arial";
        s.base_font_size = s.full_screen ? 64 : 48;

        HomeInRenderer* renderer = GetActiveRenderer();
        if (renderer) renderer->UpdateSettings(s);
    };

    connect(align_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), update_renderer_settings);
    connect(fullscreen_checkbox, &QCheckBox::toggled, update_renderer_settings);

    setLayout(layout);
}

void HomeInDock::StartTranscription() {
    // Note: In v1.0, we assume the model is in the plugin's data directory
    char *model_path = obs_module_file("models/ggml-tiny.en.bin");
    
    if (model_path && stt_engine.Initialize(model_path)) {
        stt_engine.Start([this](const std::string& text, bool is_partial) {
            // Signal to main thread to update UI
            QMetaObject::invokeMethod(this, "AppendTranscript", Qt::QueuedConnection, Q_ARG(std::string, text));
        });
    } else {
        obs_log(LOG_ERROR, "STT Engine failed to start. Model not found at %s", model_path ? model_path : "NULL");
    }
    
    if (model_path) bfree(model_path);
}

void HomeInDock::AppendTranscript(const std::string& text) {
    transcript_view->moveCursor(QTextCursor::End);
    transcript_view->insertPlainText(QString::fromStdString(text) + " ");
    transcript_view->verticalScrollBar()->setValue(transcript_view->verticalScrollBar()->maximum());

    CheckForReferences(text);
    CheckForLyrics(text);
}

void HomeInDock::CheckForLyrics(const std::string& text) {
    // Only search if the transcript seems long enough to be a lyric line
    if (text.length() < 15) return;

    lyrics_engine.Search(text, false, [this](const std::vector<SongLyric>& results) {
        if (!results.empty()) {
            QMetaObject::invokeMethod(this, "ShowLyricsResults", Qt::QueuedConnection, Q_ARG(std::vector<SongLyric>, results));
        }
    });
}

void HomeInDock::CheckForReferences(const std::string& text) {
    // Strategy 1: Regex for direct references (John 3:16)
    auto refs = ref_parser.Parse(text);
    bool found_reference = false;
    for (const auto& ref : refs) {
        BibleVerse verse;
        if (bible_db.GetVerse(ref.book, ref.chapter, ref.verse_start, "KJV", verse)) {
            ShowBibleSuggestion(verse.book_name, verse.chapter, verse.verse, verse.text);
            found_reference = true;
        }
    }

    // Strategy 2: Fuzzy FTS5 search for direct quotes (if no reference was found)
    if (!found_reference && text.length() > 20) {
        PerformFuzzySearch(text);
    }
}

void HomeInDock::PerformFuzzySearch(const std::string& text) {
    auto results = bible_db.SearchVerses(text, 1);
    if (!results.empty()) {
        const auto& v = results[0];
        ShowBibleSuggestion(v.book_name, v.chapter, v.verse, v.text);
    }
}

void HomeInDock::ShowBibleSuggestion(const std::string& book, int chapter, int verse, const std::string& text) {
    suggestion_label->setText(QString("Detected: %1 %2:%3").arg(QString::fromStdString(book)).arg(chapter).arg(verse));
    bible_suggestion_view->setText(QString::fromStdString(text));
}

void HomeInDock::SearchLyrics(const std::string& query) {
    lyrics_engine.Search(query, allow_web_checkbox->isChecked(), [this](const std::vector<SongLyric>& results) {
        QMetaObject::invokeMethod(this, "ShowLyricsResults", Qt::QueuedConnection, Q_ARG(std::vector<SongLyric>, results));
    });
}

void HomeInDock::ShowLyricsResults(const std::vector<SongLyric>& results) {
    if (results.empty()) {
        lyrics_result_view->setText("No songs found. Try a different title or turn on Web Search.");
        return;
    }

    const auto& s = results[0];
    
    // Split lyrics by double newline to identify verses/stanzas
    current_song_lines.clear();
    std::string content = s.content;
    size_t pos = 0;
    while ((pos = content.find("\n\n")) != std::string::npos) {
        current_song_lines.push_back(content.substr(0, pos));
        content.erase(0, pos + 2);
    }
    if (!content.empty()) current_song_lines.push_back(content);
    
    current_verse_index = 0;

    lyrics_result_view->setText(QString("Title: %1\nArtist: %2\nSource: %3\n\n%4")
        .arg(QString::fromStdString(s.title))
        .arg(QString::fromStdString(s.artist))
        .arg(QString::fromStdString(s.source))
        .arg(QString::fromStdString(current_song_lines[0])));
}

#include "moc_homein-dock.cpp"
