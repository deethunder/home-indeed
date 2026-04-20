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
#include <QStyle>
#include <QGroupBox>
#include <QFrame>
#include <QTimer>
#include <QScrollArea>
#include "../renderer/homein-renderer.hpp"
#include "../audio/homein-audio.hpp"

HomeInDock::HomeInDock(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(250, 150);
    
    // Initialize Databases
    char *db_path = obs_module_file("homein-bible.db");
    if (db_path) {
        bible_db.Open(db_path);
        bfree(db_path);
    }

    char *lyrics_db_path = obs_module_file("homein-lyrics.db");
    if (lyrics_db_path) {
        lyrics_engine.Initialize(lyrics_db_path);
        bfree(lyrics_db_path);
    }

    SetupUI();
    PopulateTranslations();
    
    // Setup Testing Timer
    level_timer = new QTimer(this);
    connect(level_timer, &QTimer::timeout, this, &HomeInDock::UpdateAudioTest);
    level_timer->start(100); // 10fps for smooth meter

    // Start Update Check
    connect(&updater, &HomeInUpdateChecker::UpdateAvailable, [this](const QString& version, const QString& notes, const QString& url) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Home Indeed Update Available");
        msgBox.setText(QString("A new version (%1) of Home Indeed is available!").arg(version));
        msgBox.setInformativeText("Would you like to visit the download page?");
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
    QVBoxLayout *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(5, 5, 5, 5);
    main_layout->setSpacing(2);

    view_stack = new QStackedWidget(this);
    
    // Wrap pages in Scroll Areas for responsiveness
    QScrollArea *scroll_area = new QScrollArea(this);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);
    scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    main_layout->addWidget(scroll_area);
    scroll_area->setWidget(view_stack);

    // --- Page 1: Navigation Tabs ---
    tabs_page = new QWidget();
    QVBoxLayout *tabs_layout = new QVBoxLayout(tabs_page);
    tabs_layout->setContentsMargins(0, 0, 0, 0);

    tabs_widget = new QTabWidget(this);
    tabs_layout->addWidget(tabs_widget);

    // Tab: Transcript
    QWidget *transcript_tab = new QWidget();
    QVBoxLayout *t_layout = new QVBoxLayout(transcript_tab);
    transcript_view = new QTextEdit(this);
    transcript_view->setReadOnly(true);
    transcript_view->setStyleSheet("background-color: #1e1e1e; color: #ffffff;");
    t_layout->addWidget(transcript_view);
    tabs_widget->addTab(transcript_tab, "Transcript");

    // Tab: Bible
    QWidget *bible_tab = new QWidget();
    QVBoxLayout *b_layout = new QVBoxLayout(bible_tab);
    QHBoxLayout *search_layout = new QHBoxLayout();
    bible_search_input = new QLineEdit(this);
    bible_search_input->setPlaceholderText("Search Scripture...");
    search_layout->addWidget(bible_search_input);
    QPushButton *search_btn = new QPushButton("Search", this);
    search_layout->addWidget(search_btn);
    b_layout->addLayout(search_layout);
    suggestion_label = new QLabel("No verse detected...", this);
    b_layout->addWidget(suggestion_label);
    bible_suggestion_view = new QTextEdit(this);
    bible_suggestion_view->setReadOnly(true);
    bible_suggestion_view->setStyleSheet("background-color: #1e2a1e; color: #aaffaa;");
    b_layout->addWidget(bible_suggestion_view);
    push_btn = new QPushButton("Push to Screen", this);
    push_btn->setStyleSheet("background-color: #2a4a2a; color: white; font-weight: bold;");
    b_layout->addWidget(push_btn);
    tabs_widget->addTab(bible_tab, "Bible");

    // Tab: Lyrics
    QWidget *lyrics_tab = new QWidget();
    QVBoxLayout *l_layout = new QVBoxLayout(lyrics_tab);
    QHBoxLayout *l_search_layout = new QHBoxLayout();
    lyrics_search_input = new QLineEdit(this);
    lyrics_search_input->setPlaceholderText("Search Song...");
    l_search_layout->addWidget(lyrics_search_input);
    QPushButton *l_search_btn = new QPushButton("Find", this);
    l_search_layout->addWidget(l_search_btn);
    l_layout->addLayout(l_search_layout);
    allow_web_checkbox = new QCheckBox("Web Search (LRCLIB)", this);
    allow_web_checkbox->setChecked(true);
    l_layout->addWidget(allow_web_checkbox);
    lyrics_result_view = new QTextEdit(this);
    lyrics_result_view->setReadOnly(true);
    l_layout->addWidget(lyrics_result_view);
    QHBoxLayout *stepper_layout = new QHBoxLayout();
    prev_verse_btn = new QPushButton("PREV", this);
    next_verse_btn = new QPushButton("NEXT", this);
    stepper_layout->addWidget(prev_verse_btn);
    stepper_layout->addWidget(next_verse_btn);
    l_layout->addLayout(stepper_layout);
    tabs_widget->addTab(lyrics_tab, "Lyrics");

    // Tab: Queue
    QWidget *queue_tab = new QWidget();
    QVBoxLayout *q_layout = new QVBoxLayout(queue_tab);
    queue_list = new QListWidget(this);
    queue_list->setStyleSheet("background-color: #1a1a1a; color: #ffffff;");
    q_layout->addWidget(queue_list);
    tabs_widget->addTab(queue_tab, "Queue");

    view_stack->addWidget(tabs_page);

    // --- Page 2: Settings View ---
    settings_page = new QWidget();
    SetupSettingsView(settings_page);
    view_stack->addWidget(settings_page);

    // --- Bottom Toolbar ---
    SetupToolbar(main_layout);

    // --- Connections ---
    connect(search_btn, &QPushButton::clicked, [this]() { 
        std::string text = bible_search_input->text().toStdString();
        auto refs = ref_parser.Parse(text);
        if (refs.empty()) PerformFuzzySearch(text);
        else CheckForReferences(text);
    });
    connect(l_search_btn, &QPushButton::clicked, [this]() { SearchLyrics(lyrics_search_input->text().toStdString()); });
    connect(push_btn, &QPushButton::clicked, [this]() {
        HomeInRenderer* r = GetActiveRenderer();
        if (r) r->SetText(suggestion_label->text().toStdString() + "\n" + bible_suggestion_view->toPlainText().toStdString());
    });
    connect(prev_verse_btn, &QPushButton::clicked, [this]() {
        if (current_verse_index > 0) { current_verse_index--; lyrics_result_view->setText(QString::fromStdString(current_song_lines[current_verse_index])); }
    });
    connect(next_verse_btn, &QPushButton::clicked, [this]() {
        if (current_verse_index < (int)current_song_lines.size() - 1) { current_verse_index++; lyrics_result_view->setText(QString::fromStdString(current_song_lines[current_verse_index])); }
    });
    connect(queue_list, &QListWidget::itemDoubleClicked, this, &HomeInDock::UpdateOverlayFromSelection);

    setLayout(main_layout);
}

void HomeInDock::SetupToolbar(QVBoxLayout *main_layout) {
    QFrame *toolbar = new QFrame(this);
    toolbar->setFrameShape(QFrame::StyledPanel);
    toolbar->setStyleSheet("QFrame { background-color: #2c2c2e; border-top: 1px solid #444; } QPushButton { border: none; padding: 5px; color: #ccc; font-size: 16px; } QPushButton:hover { color: white; background-color: #3a3a3c; }");
    QHBoxLayout *t_layout = new QHBoxLayout(toolbar);
    t_layout->setContentsMargins(5, 2, 5, 2);
    t_layout->setSpacing(8);

    // Mic Hub
    mic_btn = new QPushButton("🔴 LISTEN", this);
    mic_btn->setToolTip("Start Artificial Intelligence Listening");
    mic_btn->setStyleSheet("color: #ff4444; font-weight: bold; border: 1px solid #444; border-radius: 4px; padding-left: 10px; padding-right: 10px;");
    
    pause_btn = new QPushButton("⏸️", this);
    pause_btn->setToolTip("Pause Listening");
    pause_btn->setEnabled(false);

    // Focus Hub
    focus_combo = new QComboBox(this);
    focus_combo->addItem("🎯 Auto", (int)FocusMode::Auto);
    focus_combo->addItem("📖 Bible", (int)FocusMode::Bible);
    focus_combo->addItem("🎵 Songs", (int)FocusMode::Songs);
    focus_combo->setStyleSheet("background: #1a1a1a; color: #aaa; border: 1px solid #444;");

    QPushButton *gear_btn = new QPushButton("⚙️", this);
    gear_btn->setToolTip("Settings");

    QPushButton *add_btn = new QPushButton("+", this);
    add_btn->setToolTip("Queue selection");
    QPushButton *del_btn = new QPushButton("🗑️", this);

    t_layout->addWidget(mic_btn);
    t_layout->addWidget(pause_btn);
    t_layout->addSpacing(10);
    t_layout->addWidget(new QLabel("Focus:", this));
    t_layout->addWidget(focus_combo);
    t_layout->addStretch();
    t_layout->addWidget(add_btn);
    t_layout->addWidget(del_btn);
    t_layout->addWidget(gear_btn);

    main_layout->addWidget(toolbar);

    // Connections
    connect(mic_btn, &QPushButton::clicked, this, &HomeInDock::OnToggleMic);
    connect(pause_btn, &QPushButton::clicked, this, &HomeInDock::OnTogglePause);
    connect(focus_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        current_focus = (FocusMode)focus_combo->itemData(index).toInt();
    });
    connect(add_btn, &QPushButton::clicked, this, &HomeInDock::AddToQueue);
    connect(del_btn, &QPushButton::clicked, this, &HomeInDock::RemoveFromQueue);
    connect(gear_btn, &QPushButton::clicked, this, &HomeInDock::ToggleSettings);
}

void HomeInDock::SetupSettingsView(QWidget *parent) {
    QVBoxLayout *layout = new QVBoxLayout(parent);
    
    QLabel *header = new QLabel("General Settings", parent);
    header->setStyleSheet("font-size: 18px; font-weight: bold; color: #0078d7;");
    layout->addWidget(header);

    // Display Group
    QGroupBox *disp_group = new QGroupBox("Overlay Display", parent);
    QVBoxLayout *d_layout = new QVBoxLayout(disp_group);
    
    QHBoxLayout *v_layout = new QHBoxLayout();
    v_layout->addWidget(new QLabel("Bible Version:", parent));
    bible_version_combo = new QComboBox(parent);
    bible_version_combo->addItem("King James Version (KJV)");
    bible_version_combo->addItem("New Intl Version (NIV) - Requires Data");
    v_layout->addWidget(bible_version_combo);
    d_layout->addLayout(v_layout);

    QHBoxLayout *a_layout = new QHBoxLayout();
    a_layout->addWidget(new QLabel("Alignment:", parent));
    align_combo = new QComboBox(parent);
    align_combo->addItem("Center", (int)Qt::AlignCenter);
    align_combo->addItem("Left", (int)Qt::AlignLeft);
    align_combo->addItem("Right", (int)Qt::AlignRight);
    a_layout->addWidget(align_combo);
    d_layout->addLayout(a_layout);

    fullscreen_checkbox = new QCheckBox("Full Screen Mode", parent);
    d_layout->addWidget(fullscreen_checkbox);
    layout->addWidget(disp_group);

    // Audio Help Group
    QGroupBox *help_group = new QGroupBox("Audio Setup Guide", parent);
    QVBoxLayout *h_layout = new QVBoxLayout(help_group);
    QLabel *help_text = new QLabel(
        "<b>Step-by-Step Audio Activation:</b><br>"
        "1. Add your Mic or Capture Card to OBS.<br>"
        "2. Click the gear icon in the <b>Audio Mixer</b>.<br>"
        "3. Choose <b>Filters</b> -> <b>+</b> -> <b>Home Indeed Audio Tap</b>.<br>"
        "4. This plugin will now 'listen' to that source.", parent);
    help_text->setWordWrap(true);
    h_layout->addWidget(help_text);
    layout->addWidget(help_group);

    // Audio Test Group
    QGroupBox *test_group = new QGroupBox("Live Audio Test", parent);
    QVBoxLayout *t_layout = new QVBoxLayout(test_group);
    
    QLabel *m_label = new QLabel("Mic Activity:", parent);
    t_layout->addWidget(m_label);
    
    audio_level_bar = new QProgressBar(parent);
    audio_level_bar->setRange(0, 100);
    audio_level_bar->setTextVisible(false);
    audio_level_bar->setStyleSheet("QProgressBar::chunk { background-color: #2aee2a; }");
    t_layout->addWidget(audio_level_bar);
    
    last_word_field = new QLineEdit(parent);
    last_word_field->setReadOnly(true);
    last_word_field->setPlaceholderText("Waiting for speech...");
    last_word_field->setStyleSheet("background-color: #000; color: #2aee2a; font-weight: bold; border: 1px solid #333;");
    t_layout->addWidget(last_word_field);
    
    layout->addWidget(test_group);

    layout->addStretch();
    
    QPushButton *back_btn = new QPushButton("Back to View", parent);
    back_btn->setStyleSheet("background-color: #444; color: white; padding: 10px;");
    connect(back_btn, &QPushButton::clicked, this, &HomeInDock::ToggleSettings);
    layout->addWidget(back_btn);

    // Update triggers
    auto update_s = [this]() {
        OverlaySettings s;
        s.alignment = (Qt::Alignment)align_combo->currentData().toInt();
        s.full_screen = fullscreen_checkbox->isChecked();
        s.base_font_size = s.full_screen ? 64 : 48;
        HomeInRenderer* r = GetActiveRenderer();
        if (r) r->UpdateSettings(s);
    };
    connect(align_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), update_s);
    connect(fullscreen_checkbox, &QCheckBox::toggled, update_s);
}

void HomeInDock::AddToQueue() {
    QString text;
    if (tabs_widget->currentIndex() == 1) { // Bible
        text = suggestion_label->text() + ": " + bible_suggestion_view->toPlainText();
    } else if (tabs_widget->currentIndex() == 2) { // Lyrics
        text = lyrics_result_view->toPlainText();
    } else {
        text = transcript_view->toPlainText().split('\n').last();
    }
    
    if (!text.isEmpty()) {
        queue_list->addItem(text);
        tabs_widget->setCurrentIndex(3); // Switch to queue tab
    }
}

void HomeInDock::RemoveFromQueue() {
    delete queue_list->currentItem();
}

void HomeInDock::MoveQueueUp() {
    int row = queue_list->currentRow();
    if (row > 0) {
        QListWidgetItem *item = queue_list->takeItem(row);
        queue_list->insertItem(row - 1, item);
        queue_list->setCurrentRow(row - 1);
    }
}

void HomeInDock::MoveQueueDown() {
    int row = queue_list->currentRow();
    if (row < queue_list->count() - 1) {
        QListWidgetItem *item = queue_list->takeItem(row);
        queue_list->insertItem(row + 1, item);
        queue_list->setCurrentRow(row + 1);
    }
}

void HomeInDock::UpdateOverlayFromSelection() {
    if (queue_list->currentItem()) {
        HomeInRenderer *r = GetActiveRenderer();
        if (r) r->SetText(queue_list->currentItem()->text().toStdString());
    }
}

void HomeInDock::ToggleSettings() {
    if (view_stack->currentIndex() == 0) view_stack->setCurrentIndex(1);
    else view_stack->setCurrentIndex(0);
}

void HomeInDock::UpdateAudioTest() {
    HomeInAudioHandler* handler = GetAudioHandler();
    if (handler) {
        float level = handler->GetLastLevel();
        audio_level_bar->setValue(static_cast<int>(level * 100.0f));
    }
}

void HomeInDock::OnToggleMic() {
    if (!mic_active) {
        StartTranscription();
        mic_active = true;
        mic_btn->setText("⬛ STOP");
        mic_btn->setStyleSheet("color: white; background-color: #ff4444; font-weight: bold; padding: 5px;");
        pause_btn->setEnabled(true);
    } else {
        StopTranscription();
        mic_active = false;
        mic_btn->setText("🔴 LISTEN");
        mic_btn->setStyleSheet("color: #ff4444; font-weight: bold; border: 1px solid #444; padding: 5px;");
        pause_btn->setEnabled(false);
        mic_paused = false;
        pause_btn->setText("⏸️");
    }
}

void HomeInDock::OnTogglePause() {
    mic_paused = !mic_paused;
    stt_engine.SetPaused(mic_paused);
    pause_btn->setText(mic_paused ? "▶️ RESUME" : "⏸️ PAUSE");
    pause_btn->setStyleSheet(mic_paused ? "color: #ffaa00; font-weight: bold;" : "");
}

void HomeInDock::StartTranscription() {
    char *model_path = obs_module_file("models/ggml-tiny.en.bin");
    if (model_path && stt_engine.Initialize(model_path)) {
        stt_engine.Start([this](const std::string& text, bool is_partial) {
            QMetaObject::invokeMethod(this, "AppendTranscript", Qt::QueuedConnection, Q_ARG(std::string, text));
        });
    } else {
        QMessageBox::warning(this, "STT Error", "Could not load the AI model (ggml-tiny.en.bin). Please ensure the models folder was copied correctly.");
    }
    if (model_path) bfree(model_path);
}

void HomeInDock::StopTranscription() {
    stt_engine.Stop();
    transcript_view->append("\n--- Listening Session Stopped ---");
}

void HomeInDock::PopulateTranslations() {
    std::vector<std::string> versions = bible_db.GetTranslations();
    bible_version_combo->clear();
    for (const auto& v : versions) {
        bible_version_combo->addItem(QString::fromStdString(v));
    }
    // Set default to KJV if available
    int idx = bible_version_combo->findText("KJV");
    if (idx >= 0) bible_version_combo->setCurrentIndex(idx);
}

void HomeInDock::AppendTranscript(const std::string& text) {
    transcript_view->moveCursor(QTextCursor::End);
    transcript_view->insertPlainText(QString::fromStdString(text) + " ");
    transcript_view->verticalScrollBar()->setValue(transcript_view->verticalScrollBar()->maximum());
    
    // Update Test UI
    if (last_word_field) {
        last_word_field->setText(QString::fromStdString(text));
    }

    CheckForReferences(text);
    CheckForLyrics(text);
}

void HomeInDock::SetFocusMode(FocusMode mode) {
    current_focus = mode;
}

void HomeInDock::CheckForLyrics(const std::string& text) {
    if (current_focus == FocusMode::Bible) return; // Completely skip if focused on Bible
    if (text.length() < 15) return;
    lyrics_engine.Search(text, false, [this](const std::vector<SongLyric>& results) {
        if (!results.empty()) QMetaObject::invokeMethod(this, "ShowLyricsResults", Qt::QueuedConnection, Q_ARG(std::vector<SongLyric>, results));
    });
}

void HomeInDock::CheckForReferences(const std::string& text) {
    if (current_focus == FocusMode::Songs) return; // Completely skip if focused on Songs
    auto refs = ref_parser.Parse(text);
    bool found_reference = false;
    std::string version = bible_version_combo->currentText().toStdString();

    for (const auto& ref : refs) {
        BibleVerse verse;
        if (bible_db.GetVerse(ref.book, ref.chapter, ref.verse_start, version, verse)) {
            ShowBibleSuggestion(verse.book_name, verse.chapter, verse.verse, verse.text);
            found_reference = true;
        }
    }
    if (!found_reference && text.length() > 20) PerformFuzzySearch(text);
}

void HomeInDock::PerformFuzzySearch(const std::string& text) {
    auto results = bible_db.SearchVerses(text, 1);
    if (!results.empty()) {
        const auto& v = results[0];
        ShowBibleSuggestion(v.book_name, v.chapter, v.verse, v.text);
    }
}

void HomeInDock::ShowBibleSuggestion(const std::string& book, int chapter, int verse, const std::string& text) {
    suggestion_label->setText(QString("%1 %2:%3").arg(QString::fromStdString(book)).arg(chapter).arg(verse));
    bible_suggestion_view->setText(QString::fromStdString(text));
}

void HomeInDock::SearchLyrics(const std::string& query) {
    lyrics_engine.Search(query, allow_web_checkbox->isChecked(), [this](const std::vector<SongLyric>& results) {
        QMetaObject::invokeMethod(this, "ShowLyricsResults", Qt::QueuedConnection, Q_ARG(std::vector<SongLyric>, results));
    });
}

void HomeInDock::ShowLyricsResults(const std::vector<SongLyric>& results) {
    if (results.empty()) { lyrics_result_view->setText("Not found."); return; }
    const auto& s = results[0];
    current_song_lines.clear();
    std::string content = s.content;
    size_t pos = 0;
    while ((pos = content.find("\n\n")) != std::string::npos) { current_song_lines.push_back(content.substr(0, pos)); content.erase(0, pos + 2); }
    if (!content.empty()) current_song_lines.push_back(content);
    current_verse_index = 0;
    lyrics_result_view->setText(QString::fromStdString(current_song_lines[0]));
}
