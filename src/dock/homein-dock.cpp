// homein-dock.cpp — Fixed version
// Changes from original:
//   FIX #1/#5:  PopulateTranslations stores abbreviation as item data;
//               SetupSettingsView no longer hardcodes translation items.
//               All version reads use currentData() not currentText().
//   FIX #8:     OnImportEasyWorship guards against unopened lyrics DB.
//   FIX #11:    Update check delayed 3 s to avoid partial-construction race.
//   FIX #3:     Added diagnostic log when DB paths are not found.

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
#include <QFileDialog>
#include <QStandardPaths>
#include "../renderer/homein-renderer.hpp"
#include "../audio/homein-audio.hpp"
#include "../database/homein-importer.hpp"

HomeInDock::HomeInDock(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(250, 150);

    // --- Open Databases ---
    char *db_path = obs_module_file("homein-bible.db");
    if (db_path) {
        bible_db.Open(db_path);
        bfree(db_path);
    } else {
        blog(LOG_ERROR, "HomeIndeed: homein-bible.db NOT FOUND via obs_module_file. "
                        "Ensure the file is in the OBS plugin data directory.");
    }

    char *lyrics_db_path = obs_module_file("homein-lyrics.db");
    if (lyrics_db_path) {
        lyrics_engine.Initialize(lyrics_db_path);
        bfree(lyrics_db_path);
    } else {
        blog(LOG_ERROR, "HomeIndeed: homein-lyrics.db NOT FOUND via obs_module_file. "
                        "EasyWorship import and web lyrics caching will be unavailable.");
    }

    SetupUI();
    PopulateTranslations();

    // Audio level meter at 10 fps
    level_timer = new QTimer(this);
    connect(level_timer, &QTimer::timeout, this, &HomeInDock::UpdateAudioTest);
    level_timer->start(100);

    // Update check connection
    connect(&updater, &HomeInUpdateChecker::UpdateAvailable,
            [this](const QString& version, const QString& notes, const QString& url) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Home Indeed Update Available");
        msgBox.setText(QString("A new version (%1) of Home Indeed is available!").arg(version));
        msgBox.setInformativeText("Would you like to visit the download page?");
        msgBox.setStandardButtons(QMessageBox::Open | QMessageBox::Close);
        msgBox.setDefaultButton(QMessageBox::Open);
        if (msgBox.exec() == QMessageBox::Open)
            QDesktopServices::openUrl(QUrl(url));
    });

    // FIX #11: Delay update check 3 s so 'this' is fully constructed before
    // any signal can fire back into the lambda that captures it.
    QTimer::singleShot(3000, this, [this]() {
        updater.CheckForUpdates("DeeThunder/Home-Indeed");
    });
}

HomeInDock::~HomeInDock() {
    stt_engine.Stop();
}

void HomeInDock::SetupUI() {
    this->setStyleSheet(
        "QWidget { background-color: #1a1a1b; color: #e8eaed; font-family: 'Segoe UI', sans-serif; }"
        "QTabWidget::pane { border: 1px solid #3c4043; top: -1px; background: #202124; }"
        "QTabBar::tab { background: #202124; border: 1px solid #3c4043; padding: 10px 15px; min-width: 80px; color: #9aa0a6; }"
        "QTabBar::tab:selected { background: #35363a; border-bottom-color: #8ab4f8; color: #8ab4f8; font-weight: bold; }"
        "QTabBar::tab:hover { background: #35363a; color: #ffffff; }"
        "QLineEdit { background-color: #202124; border: 1px solid #3c4043; border-radius: 4px; padding: 5px; color: #ffffff; selection-background-color: #8ab4f8; }"
        "QLineEdit:focus { border: 1px solid #8ab4f8; }"
        "QTextEdit { background-color: #17181a; border: 1px solid #3c4043; border-radius: 4px; color: #bdc1c6; padding: 8px; line-height: 1.4; }"
        "QPushButton { background-color: #3c4043; border: none; border-radius: 4px; padding: 6px 12px; color: #e8eaed; font-weight: 500; }"
        "QPushButton:hover { background-color: #4f5357; }"
        "QPushButton:pressed { background-color: #5f6368; }"
        "QPushButton#pushBtn { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1a73e8, stop:1 #174ea6); color: white; border: 1px solid #174ea6; font-weight: bold; margin-top: 5px; }"
        "QPushButton#pushBtn:hover { background: #1a73e8; }"
        "QPushButton#searchBtn { background-color: #35363a; border: 1px solid #3c4043; }"
        "QCheckBox { spacing: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #3c4043; border-radius: 3px; background: #202124; }"
        "QCheckBox::indicator:checked { background: #8ab4f8; border-color: #8ab4f8; }"
        "QGroupBox { font-weight: bold; border: 1px solid #3c4043; border-radius: 6px; margin-top: 12px; padding-top: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; color: #8ab4f8; padding: 0 3px; }"
        "QListWidget { background-color: #17181a; border: 1px solid #3c4043; border-radius: 4px; }"
        "QListWidget::item { padding: 8px; border-bottom: 1px solid #202124; }"
        "QListWidget::item:selected { background-color: #303134; color: #8ab4f8; }"
        "QProgressBar { background-color: #202124; border: 1px solid #3c4043; border-radius: 4px; height: 12px; text-align: center; }"
        "QProgressBar::chunk { background-color: #81c995; border-radius: 3px; }"
    );

    QVBoxLayout *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(5, 5, 5, 5);
    main_layout->setSpacing(2);

    view_stack = new QStackedWidget(this);

    QScrollArea *scroll_area = new QScrollArea(this);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);
    scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    main_layout->addWidget(scroll_area);
    scroll_area->setWidget(view_stack);

    // --- Page 1: Tabs ---
    tabs_page = new QWidget();
    QVBoxLayout *tabs_layout = new QVBoxLayout(tabs_page);
    tabs_layout->setContentsMargins(0, 0, 0, 0);

    tabs_widget = new QTabWidget(this);
    tabs_layout->addWidget(tabs_widget);

    // Transcript tab
    QWidget *transcript_tab = new QWidget();
    QVBoxLayout *t_layout = new QVBoxLayout(transcript_tab);
    transcript_view = new QTextEdit(this);
    transcript_view->setReadOnly(true);
    t_layout->addWidget(transcript_view);
    tabs_widget->addTab(transcript_tab, "Transcript");

    // Bible tab
    QWidget *bible_tab = new QWidget();
    QVBoxLayout *b_layout = new QVBoxLayout(bible_tab);
    QHBoxLayout *search_layout = new QHBoxLayout();
    bible_search_input = new QLineEdit(this);
    bible_search_input->setPlaceholderText("Type book (e.g. Gen)...");
    search_layout->addWidget(bible_search_input);
    QPushButton *search_btn = new QPushButton("Search", this);
    search_btn->setObjectName("searchBtn");
    search_layout->addWidget(search_btn);
    QPushButton *help_btn = new QPushButton("?", this);
    help_btn->setObjectName("searchBtn");
    help_btn->setFixedWidth(30);
    help_btn->setToolTip("How to use Home Indeed");
    search_layout->addWidget(help_btn);
    b_layout->addLayout(search_layout);

    suggestion_label = new QLabel("No verse detected...", this);
    suggestion_label->setStyleSheet("font-weight: bold; color: #5294e2;");
    b_layout->addWidget(suggestion_label);

    bible_grid_container = new QWidget(this);
    bible_grid_layout = new QGridLayout(bible_grid_container);
    bible_grid_layout->setSpacing(2);
    bible_grid_container->setVisible(false);
    b_layout->addWidget(bible_grid_container);

    bible_suggestion_view = new QTextEdit(this);
    bible_suggestion_view->setReadOnly(true);
    bible_suggestion_view->setMaximumHeight(120); // Prevent stretching
    b_layout->addWidget(bible_suggestion_view);

    QHBoxLayout *action_layout = new QHBoxLayout();

    bible_prev_btn = new QPushButton("▲", this);
    bible_prev_btn->setToolTip("Preceding Verse");
    bible_prev_btn->setFixedWidth(35);
    bible_prev_btn->setFixedHeight(35);

    bible_next_btn = new QPushButton("▼", this);
    bible_next_btn->setToolTip("Succeeding Verse");
    bible_next_btn->setFixedWidth(35);
    bible_next_btn->setFixedHeight(35);

    QPushButton *clear_btn = new QPushButton("Clear", this);
    clear_btn->setStyleSheet("background-color: #5f6368; color: white; font-weight: bold; border-radius: 4px;");
    clear_btn->setFixedHeight(35);
    
    push_btn = new QPushButton("Push live", this);
    push_btn->setObjectName("pushBtn");
    push_btn->setFixedHeight(35);

    action_layout->addWidget(bible_prev_btn);
    action_layout->addWidget(bible_next_btn);
    action_layout->addWidget(clear_btn);
    action_layout->addWidget(push_btn);
    b_layout->addLayout(action_layout);
    
    // Add stretch at the very bottom so everything packs tightly to the top
    b_layout->addStretch();
    
    tabs_widget->addTab(bible_tab, "Bible");
    
    // Connect clear button immediately
    connect(clear_btn, &QPushButton::clicked, [this]() {
        suggestion_label->setText("No verse detected...");
        bible_suggestion_view->clear();
        current_chapter_verses.clear();
        current_bible_verse_index = -1;
        HomeInRenderer* r = GetActiveRenderer();
        if (r) {
            r->SetText("");
        }
    });

    connect(bible_prev_btn, &QPushButton::clicked, [this]() {
        if (current_bible_verse_index > 0) {
            ShowBibleVerseAtIndex(current_bible_verse_index - 1);
        }
    });

    connect(bible_next_btn, &QPushButton::clicked, [this]() {
        if (current_bible_verse_index >= 0 && current_bible_verse_index < (int)current_chapter_verses.size() - 1) {
            ShowBibleVerseAtIndex(current_bible_verse_index + 1);
        }
    });

    // Lyrics tab
    QWidget *lyrics_tab = new QWidget();
    QVBoxLayout *l_layout = new QVBoxLayout(lyrics_tab);
    QHBoxLayout *l_search_layout = new QHBoxLayout();
    lyrics_search_input = new QLineEdit(this);
    lyrics_search_input->setPlaceholderText("Search Song...");
    l_search_layout->addWidget(lyrics_search_input);
    QPushButton *l_search_btn = new QPushButton("Find", this);
    l_search_btn->setObjectName("searchBtn");
    l_search_layout->addWidget(l_search_btn);
    l_layout->addLayout(l_search_layout);

    QHBoxLayout *l_options_layout = new QHBoxLayout();
    allow_web_checkbox = new QCheckBox("Web Search (LRCLIB)", this);
    allow_web_checkbox->setChecked(true);
    l_options_layout->addWidget(allow_web_checkbox);
    QPushButton *import_btn = new QPushButton("📥 Import EasyWorship", this);
    import_btn->setStyleSheet("font-weight: bold; color: #5294e2;");
    l_options_layout->addWidget(import_btn);
    l_layout->addLayout(l_options_layout);

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

    // Queue tab
    QWidget *queue_tab = new QWidget();
    QVBoxLayout *q_layout = new QVBoxLayout(queue_tab);
    queue_list = new QListWidget(this);
    queue_list->setStyleSheet("background-color: #1a1a1a; color: #ffffff;");
    q_layout->addWidget(queue_list);
    tabs_widget->addTab(queue_tab, "Queue");

    view_stack->addWidget(tabs_page);

    // --- Page 2: Settings ---
    settings_page = new QWidget();
    SetupSettingsView(settings_page);
    view_stack->addWidget(settings_page);

    // --- Toolbar ---
    SetupToolbar(main_layout);

    // --- Connections ---
    connect(search_btn, &QPushButton::clicked, [this]() {
        std::string text = bible_search_input->text().toStdString();
        int chapters = bible_db.GetChapterCount(text);
        if (chapters > 0) {
            current_search_book = text;
            PopulateChapterGrid(text, chapters);
        } else {
            auto refs = ref_parser.Parse(text);
            if (refs.empty()) PerformFuzzySearch(text);
            else CheckForReferences(text);
        }
    });
    connect(help_btn,      &QPushButton::clicked, this, &HomeInDock::OnShowHelp);
    connect(l_search_btn,  &QPushButton::clicked, [this]() {
        SearchLyrics(lyrics_search_input->text().toStdString());
    });
    connect(push_btn, &QPushButton::clicked, [this]() {
        HomeInRenderer* r = GetActiveRenderer();
        if (r && current_bible_verse_index >= 0 && current_bible_verse_index < (int)current_chapter_verses.size()) {
            const BibleVerse& v = current_chapter_verses[current_bible_verse_index];
            std::string ref = suggestion_label->text().toStdString();
            std::string body = std::to_string(v.verse) + " " + v.text;
            r->SetText(ref + "\n" + body);
        }
    });
    connect(prev_verse_btn, &QPushButton::clicked, [this]() {
        if (current_verse_index > 0) {
            current_verse_index--;
            lyrics_result_view->setText(
                QString::fromStdString(current_song_lines[current_verse_index]));
        }
    });
    connect(next_verse_btn, &QPushButton::clicked, [this]() {
        if (current_verse_index < (int)current_song_lines.size() - 1) {
            current_verse_index++;
            lyrics_result_view->setText(
                QString::fromStdString(current_song_lines[current_verse_index]));
        }
    });
    connect(queue_list, &QListWidget::itemDoubleClicked,
            this, &HomeInDock::UpdateOverlayFromSelection);
    connect(import_btn, &QPushButton::clicked, this, &HomeInDock::OnImportEasyWorship);

    setLayout(main_layout);
}

void HomeInDock::SetupToolbar(QVBoxLayout *main_layout) {
    QFrame *toolbar = new QFrame(this);
    toolbar->setFrameShape(QFrame::StyledPanel);
    toolbar->setStyleSheet(
        "QFrame { background-color: #2c2c2e; border-top: 1px solid #444; }"
        "QPushButton { border: none; padding: 5px; color: #ccc; font-size: 16px; }"
        "QPushButton:hover { color: white; background-color: #3a3a3c; }");
    QHBoxLayout *t_layout = new QHBoxLayout(toolbar);
    t_layout->setContentsMargins(5, 2, 5, 2);
    t_layout->setSpacing(8);

    mic_btn = new QPushButton("🔴 LISTEN", this);
    mic_btn->setObjectName("micBtn");
    mic_btn->setToolTip("Start Artificial Intelligence Listening");
    mic_btn->setStyleSheet(
        "QPushButton#micBtn { color: #ff6b6b; border: 1px solid #444; "
        "border-radius: 4px; padding: 5px 12px; font-weight: bold; }");

    pause_btn = new QPushButton("⏸️", this);
    pause_btn->setObjectName("searchBtn");
    pause_btn->setToolTip("Pause Listening");
    pause_btn->setEnabled(false);

    focus_combo = new QComboBox(this);
    focus_combo->addItem("🎯 Auto",   (int)FocusMode::Auto);
    focus_combo->addItem("📖 Bible",  (int)FocusMode::Bible);
    focus_combo->addItem("🎵 Songs",  (int)FocusMode::Songs);
    focus_combo->setStyleSheet(
        "background: #1a1a1a; color: #aaa; border: 1px solid #444;");

    QPushButton *gear_btn = new QPushButton("⚙️", this);
    gear_btn->setToolTip("Settings");
    QPushButton *add_btn  = new QPushButton("+",   this);
    add_btn->setToolTip("Queue selection");
    QPushButton *del_btn  = new QPushButton("🗑️", this);

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

    connect(mic_btn,    &QPushButton::clicked, this, &HomeInDock::OnToggleMic);
    connect(pause_btn,  &QPushButton::clicked, this, &HomeInDock::OnTogglePause);
    connect(focus_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
        current_focus = (FocusMode)focus_combo->itemData(index).toInt();
    });
    connect(add_btn,  &QPushButton::clicked, this, &HomeInDock::AddToQueue);
    connect(del_btn,  &QPushButton::clicked, this, &HomeInDock::RemoveFromQueue);
    connect(gear_btn, &QPushButton::clicked, this, &HomeInDock::ToggleSettings);
}

void HomeInDock::SetupSettingsView(QWidget *parent) {
    QVBoxLayout *layout = new QVBoxLayout(parent);

    QLabel *header = new QLabel("General Settings", parent);
    header->setStyleSheet("font-size: 18px; font-weight: bold; color: #0078d7;");
    layout->addWidget(header);

    QGroupBox *disp_group = new QGroupBox("Overlay Display", parent);
    QVBoxLayout *d_layout = new QVBoxLayout(disp_group);

    QHBoxLayout *v_layout = new QHBoxLayout();
    v_layout->addWidget(new QLabel("Bible Version:", parent));
    bible_version_combo = new QComboBox(parent);
    // FIX #1: Do NOT add hardcoded items here.
    // PopulateTranslations() loads the real abbreviations from the DB
    // and stores them as both text AND item data.
    // If we add items here first, PopulateTranslations clears and repopulates correctly.
    v_layout->addWidget(bible_version_combo);
    d_layout->addLayout(v_layout);

    QHBoxLayout *a_layout = new QHBoxLayout();
    a_layout->addWidget(new QLabel("Alignment:", parent));
    align_combo = new QComboBox(parent);
    align_combo->addItem("Center", (int)Qt::AlignCenter);
    align_combo->addItem("Left",   (int)Qt::AlignLeft);
    align_combo->addItem("Right",  (int)Qt::AlignRight);
    a_layout->addWidget(align_combo);
    d_layout->addLayout(a_layout);

    fullscreen_checkbox = new QCheckBox("Full Screen Mode", parent);
    d_layout->addWidget(fullscreen_checkbox);

    QGroupBox *auto_group = new QGroupBox("Intelligent Automation", parent);
    QVBoxLayout *a_group_layout = new QVBoxLayout(auto_group);
    auto_switch_tabs_checkbox = new QCheckBox("Auto-Switch Tabs on Detection", parent);
    auto_switch_tabs_checkbox->setChecked(auto_switch_tabs);
    a_group_layout->addWidget(auto_switch_tabs_checkbox);
    auto_search_checkbox = new QCheckBox("Auto-Search Databases", parent);
    auto_search_checkbox->setChecked(auto_search);
    a_group_layout->addWidget(auto_search_checkbox);
    auto_push_checkbox = new QCheckBox("Auto-Push to Screen (1st Match)", parent);
    auto_push_checkbox->setChecked(auto_push);
    a_group_layout->addWidget(auto_push_checkbox);

    layout->addWidget(auto_group);
    layout->addWidget(disp_group);

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

    QGroupBox *test_group = new QGroupBox("Live Audio Test", parent);
    QVBoxLayout *t_layout = new QVBoxLayout(test_group);
    t_layout->addWidget(new QLabel("Mic Activity:", parent));
    audio_level_bar = new QProgressBar(parent);
    audio_level_bar->setRange(0, 100);
    audio_level_bar->setTextVisible(false);
    audio_level_bar->setStyleSheet("QProgressBar::chunk { background-color: #2aee2a; }");
    t_layout->addWidget(audio_level_bar);
    last_word_field = new QLineEdit(parent);
    last_word_field->setReadOnly(true);
    last_word_field->setPlaceholderText("Waiting for speech...");
    last_word_field->setStyleSheet(
        "background-color: #000; color: #2aee2a; font-weight: bold; border: 1px solid #333;");
    t_layout->addWidget(last_word_field);
    layout->addWidget(test_group);

    layout->addStretch();

    QPushButton *back_btn = new QPushButton("Back to View", parent);
    back_btn->setStyleSheet("background-color: #444; color: white; padding: 10px;");
    connect(back_btn, &QPushButton::clicked, this, &HomeInDock::ToggleSettings);
    layout->addWidget(back_btn);

    auto update_s = [this]() {
        OverlaySettings s;
        s.alignment      = (Qt::Alignment)align_combo->currentData().toInt();
        s.full_screen    = fullscreen_checkbox->isChecked();
        s.base_font_size = s.full_screen ? 64 : 48;
        HomeInRenderer* r = GetActiveRenderer();
        if (r) r->UpdateSettings(s);
    };
    connect(align_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), update_s);
    connect(fullscreen_checkbox, &QCheckBox::toggled, update_s);
    connect(auto_switch_tabs_checkbox, &QCheckBox::toggled,
            [this](bool c){ auto_switch_tabs = c; });
    connect(auto_search_checkbox, &QCheckBox::toggled,
            [this](bool c){ auto_search = c; });
    connect(auto_push_checkbox, &QCheckBox::toggled,
            [this](bool c){ auto_push = c; });
}

// FIX #1: Stores abbreviation as BOTH item text AND item data.
// All reads now use currentData().toString() to get the clean abbreviation
// (e.g. "KJV") regardless of how the item is displayed.
void HomeInDock::PopulateTranslations() {
    std::vector<std::string> versions = bible_db.GetTranslations();
    bible_version_combo->clear();
    for (const auto& v : versions) {
        QString abbr = QString::fromStdString(v);
        bible_version_combo->addItem(abbr, abbr); // text=abbr, data=abbr
    }
    int idx = bible_version_combo->findText("KJV");
    if (idx >= 0) bible_version_combo->setCurrentIndex(idx);
}

// Helper: always read abbreviation from item data, never from display text.
std::string HomeInDock::CurrentTranslation() const {
    return bible_version_combo->currentData().toString().toStdString();
}

void HomeInDock::AddToQueue() {
    QString text;
    if (tabs_widget->currentIndex() == 1)
        text = suggestion_label->text() + ": " + bible_suggestion_view->toPlainText();
    else if (tabs_widget->currentIndex() == 2)
        text = lyrics_result_view->toPlainText();
    else
        text = transcript_view->toPlainText().split('\n').last();

    if (!text.isEmpty()) {
        queue_list->addItem(text);
        tabs_widget->setCurrentIndex(3);
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
    view_stack->setCurrentIndex(view_stack->currentIndex() == 0 ? 1 : 0);
}

void HomeInDock::UpdateAudioTest() {
    HomeInAudioHandler* handler = GetAudioHandler();
    if (handler)
        audio_level_bar->setValue(static_cast<int>(handler->GetLastLevel() * 100.0f));

    // FIX #14: PrepareTexture() must run on the Qt main thread (QPainter is
    // not thread-safe). The level_timer fires at 10 fps on the Qt main thread,
    // making it the correct place to drive texture pre-rendering.
    HomeInRenderer* r = GetActiveRenderer();
    if (r) r->PrepareTexture();
}

void HomeInDock::OnToggleMic() {
    if (!mic_active) {
        StartTranscription();
        mic_active = true;
        mic_btn->setText("⬛ STOP");
        mic_btn->setStyleSheet(
            "QPushButton#micBtn { color: white; background-color: #ff4444; "
            "border: 1px solid #ff4444; font-weight: bold; padding: 5px 12px; }");
        pause_btn->setEnabled(true);
    } else {
        StopTranscription();
        mic_active = false;
        mic_btn->setText("🔴 LISTEN");
        mic_btn->setStyleSheet(
            "QPushButton#micBtn { color: #ff6b6b; border: 1px solid #444; "
            "border-radius: 4px; padding: 5px 12px; font-weight: bold; }");
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
    const char* model_names[] = {
        "models/ggml-small.en.bin",
        "models/ggml-base.en.bin",
        "models/ggml-tiny.en.bin"
    };
    char* model_path = nullptr;

    for (const char* name : model_names) {
        model_path = obs_module_file(name);
        if (model_path) {
            blog(LOG_INFO, "HomeIndeed: Using AI model: %s", name);
            break;
        }
    }

    if (model_path && stt_engine.Initialize(model_path)) {
        stt_engine.Start([this](const std::string& text, bool /*is_partial*/) {
            QMetaObject::invokeMethod(this, "AppendTranscript",
                                      Qt::QueuedConnection,
                                      Q_ARG(std::string, text));
        });
    } else {
        QMessageBox::warning(this, "STT Error",
            "Could not load any AI model.\n\n"
            "Please place ggml-tiny.en.bin in the plugin models folder.\n"
            "Download: https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin");
    }
    if (model_path) bfree(model_path);
}

void HomeInDock::StopTranscription() {
    stt_engine.Stop();
    transcript_view->append("\n--- Listening Session Stopped ---");
}

void HomeInDock::OnImportEasyWorship() {
    // FIX #8: Guard against an uninitialised lyrics DB.
    if (!lyrics_engine.IsDBOpen()) {
        QMessageBox::critical(this, "Import Error",
            "Lyrics database is not initialised.\n\n"
            "Ensure homein-lyrics.db exists in the OBS plugin data directory:\n"
            "  obs-studio/data/obs-plugins/home-indeed/\n\n"
            "Then restart OBS and try again.");
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this, "Import Option",
        "Would you like to sync DIRECTLY from your EasyWorship database (Fastest)?",
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (reply == QMessageBox::Cancel) return;

    HomeInImporter importer(lyrics_engine.GetDB());
    int count = 0;

    if (reply == QMessageBox::Yes) {
        QString dbPath = QFileDialog::getOpenFileName(this,
            "Select EasyWorship Songs.db",
            "C:/Users/Public/Documents/Softouch/EasyWorship/Default/v6.1/Databases/Data",
            "EasyWorship Database (Songs.db)");
        if (!dbPath.isEmpty())
            count = importer.ImportFromEW7(dbPath);
    } else {
        QStringList files = QFileDialog::getOpenFileNames(this,
            "Select EasyWorship (OpenLyrics) XML Files",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            "OpenLyrics XML (*.xml)");
        for (const QString& file : files)
            count += importer.ImportFile(file);
    }

    if (count > 0) {
        QMessageBox::information(this, "Import Complete",
            QString("Successfully synced %1 songs into your local library.").arg(count));
    } else {
        QMessageBox::warning(this, "Import Result",
            "No songs were imported. Check the OBS log for details.");
    }
}

void HomeInDock::ClearBibleGrid() {
    QLayoutItem *child;
    while ((child = bible_grid_layout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
}

void HomeInDock::PopulateChapterGrid(const std::string& book_name, int count) {
    ClearBibleGrid();
    bible_suggestion_view->setVisible(false);
    bible_grid_container->setVisible(true);
    suggestion_label->setText(
        QString("Selecting Chapters for %1:").arg(QString::fromStdString(book_name)));

    int cols = 6;
    for (int i = 0; i < count; ++i) {
        QPushButton *btn = new QPushButton(QString::number(i + 1), this);
        btn->setFixedSize(35, 30);
        btn->setObjectName("searchBtn");
        btn->setStyleSheet(
            "QPushButton { font-size: 13px; font-weight: bold; "
            "background-color: #35363a; border: 1px solid #444; }"
            "QPushButton:hover { background-color: #4285f4; border-color: #4285f4; }");
        connect(btn, &QPushButton::clicked, this, &HomeInDock::OnChapterSelected);
        bible_grid_layout->addWidget(btn, i / cols, i % cols);
    }
}

void HomeInDock::OnChapterSelected() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int chapter = btn->text().toInt();

    current_chapter_verses = bible_db.GetChapterVerses(current_search_book, chapter, CurrentTranslation());
    
    if (!current_chapter_verses.empty()) {
        ShowBibleVerseAtIndex(0);
        bible_grid_container->setVisible(false);
        bible_suggestion_view->setVisible(true);
    }
}

void HomeInDock::OnShowHelp() {
    QMessageBox::information(this, "Getting Started with Home Indeed",
        "<b>1. Display to Stream:</b><br>"
        "Click the <b>(+)</b> in OBS Sources -> Select <b>'Home Indeed Overlay'</b>.<br><br>"
        "<b>2. Audio Sync:</b><br>"
        "Go to Settings -> <b>Audio Setup Guide</b> to connect your mic.<br><br>"
        "<b>3. Navigation:</b><br>"
        "Type a book like 'Gen' to see chapter buttons, or just speak!");
}

void HomeInDock::AppendTranscript(const std::string& text) {
    transcript_view->moveCursor(QTextCursor::End);
    transcript_view->insertPlainText(QString::fromStdString(text) + " ");
    transcript_view->verticalScrollBar()->setValue(
        transcript_view->verticalScrollBar()->maximum());

    if (last_word_field)
        last_word_field->setText(QString::fromStdString(text));

    CheckForReferences(text);
    CheckForLyrics(text);
}

void HomeInDock::SetFocusMode(FocusMode mode) {
    current_focus = mode;
}

void HomeInDock::CheckForLyrics(const std::string& text) {
    if (current_focus == FocusMode::Bible) return;
    if (!auto_search) return;
    if (text.length() < 15) return;

    lyrics_engine.Search(text, false, [this](const std::vector<SongLyric>& results) {
        if (!results.empty()) {
            QMetaObject::invokeMethod(this, "ShowLyricsResults",
                                      Qt::QueuedConnection,
                                      Q_ARG(std::vector<SongLyric>, results));

            const auto& s = results[0];
            QStringList lines =
                QString::fromStdString(s.content).split('\n', Qt::SkipEmptyParts);

            QMetaObject::invokeMethod(this, [this, lines]() {
                for (int i = 0; i < lines.size(); i += 2) {
                    QString chunk = lines[i];
                    if (i + 1 < lines.size()) chunk += "\n" + lines[i + 1];
                    queue_list->addItem(chunk);
                }
            }, Qt::QueuedConnection);

            if (auto_switch_tabs)
                QMetaObject::invokeMethod(tabs_widget, "setCurrentIndex",
                                          Qt::QueuedConnection, Q_ARG(int, 2));
        }
    });
}

void HomeInDock::ShowBibleVerseAtIndex(int index) {
    if (index < 0 || index >= (int)current_chapter_verses.size()) return;

    current_bible_verse_index = index;
    const BibleVerse& verse = current_chapter_verses[index];

    QString content = QString::fromStdString(verse.text);
    bible_suggestion_view->setText(content);

    QString ref = QString::fromStdString(verse.book_name) + " " +
                  QString::number(verse.chapter) + ":" +
                  QString::number(verse.verse);
    if (!verse.translation_abbr.empty()) {
        ref += " (" + QString::fromStdString(verse.translation_abbr) + ")";
    }
    suggestion_label->setText(ref);
}

void HomeInDock::CheckForReferences(const std::string& text) {
    if (current_focus == FocusMode::Songs) return;
    if (!auto_search) return;

    auto refs = ref_parser.Parse(text);
    std::string version = CurrentTranslation();

    if (!refs.empty()) {
        const auto& ref = refs[0]; // Process the first reference for chapter loading

        current_chapter_verses = bible_db.GetChapterVerses(ref.book, ref.chapter, version);

        if (!current_chapter_verses.empty()) {
            current_bible_verse_index = 0;
            for (size_t j = 0; j < current_chapter_verses.size(); ++j) {
                if (current_chapter_verses[j].verse == ref.verse_start) {
                    current_bible_verse_index = (int)j;
                    break;
                }
            }

            ShowBibleVerseAtIndex(current_bible_verse_index);

            if (auto_push) {
                HomeInRenderer* r = GetActiveRenderer();
                if (r) {
                    const BibleVerse& v = current_chapter_verses[current_bible_verse_index];
                    QString ref = QString("%1 %2:%3")
                            .arg(QString::fromStdString(v.book_name))
                            .arg(v.chapter).arg(v.verse);
                    if (!v.translation_abbr.empty()) {
                        ref += " (" + QString::fromStdString(v.translation_abbr) + ")";
                    }
                    r->SetText(ref.toStdString() + "\n" + std::to_string(v.verse) + " " + v.text);
                }
            }

            if (auto_switch_tabs)
                QMetaObject::invokeMethod(tabs_widget, "setCurrentIndex",
                                          Qt::QueuedConnection, Q_ARG(int, 1));
        }
    } else if (text.length() > 20) {
        PerformFuzzySearch(text);
    }
}

void HomeInDock::PerformFuzzySearch(const std::string& text) {
    auto results = bible_db.SearchVerses(text, 1);
    if (!results.empty()) {
        const auto& v = results[0];
        std::string version = CurrentTranslation();
        current_chapter_verses = bible_db.GetChapterVerses(v.book_name, v.chapter, version);
        
        if (!current_chapter_verses.empty()) {
            current_bible_verse_index = 0;
            for (size_t j = 0; j < current_chapter_verses.size(); ++j) {
                if (current_chapter_verses[j].verse == v.verse) {
                    current_bible_verse_index = (int)j;
                    break;
                }
            }
            ShowBibleVerseAtIndex(current_bible_verse_index);
        }
    }
}

void HomeInDock::ShowBibleSuggestion(const std::string& book, int chapter,
                                      int verse, const std::string& text) {
    suggestion_label->setText(
        QString("%1 %2:%3")
            .arg(QString::fromStdString(book)).arg(chapter).arg(verse));
    bible_suggestion_view->setText(QString::fromStdString(text));
}

void HomeInDock::SearchLyrics(const std::string& query) {
    lyrics_engine.Search(query, allow_web_checkbox->isChecked(),
        [this](const std::vector<SongLyric>& results) {
            QMetaObject::invokeMethod(this, "ShowLyricsResults",
                                      Qt::QueuedConnection,
                                      Q_ARG(std::vector<SongLyric>, results));
        });
}

void HomeInDock::ShowLyricsResults(const std::vector<SongLyric>& results) {
    if (results.empty()) { lyrics_result_view->setText("Not found."); return; }
    const auto& s = results[0];
    current_song_lines.clear();
    std::string content = s.content;
    size_t pos = 0;
    while ((pos = content.find("\n\n")) != std::string::npos) {
        current_song_lines.push_back(content.substr(0, pos));
        content.erase(0, pos + 2);
    }
    if (!content.empty()) current_song_lines.push_back(content);
    current_verse_index = 0;
    if (!current_song_lines.empty())
        lyrics_result_view->setText(
            QString::fromStdString(current_song_lines[0]));
}
