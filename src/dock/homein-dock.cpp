#include "homein-dock.hpp"
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include "../audio/homein-audio.hpp"
#include "../renderer/homein-renderer.hpp"
#include "../stt/homein-stt.hpp"
#include <QVBoxLayout>
#include <QDir>
#include <QStandardPaths>

// Include your newly isolated tabs
#include "../tabs/homein-transcript-tab.hpp"
#include "../tabs/homein-bible-tab.hpp"
#include "../tabs/homein-lyrics-tab.hpp"
#include "../tabs/homein-queue-tab.hpp"
#include "../tabs/homein-settings-tab.hpp"

// =====================================================================
// 🛠️ DEVELOPER TESTING SWITCH
// Options: WHISPER_LOCAL | VOSK_LOCAL | DEEPGRAM_CLOUD
// =====================================================================
static constexpr STTEngineType ACTIVE_STT_ENGINE = STTEngineType::DEEPGRAM_CLOUD;

HomeInDock::HomeInDock(QWidget *parent) : QWidget(parent) {
    context = std::make_shared<SermonContext>();
    ref_parser.SetContext(context);
    transcript_queue = std::make_shared<TranscriptQueue>();

    // Load Databases
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/HomeIndeed";
    QDir().mkpath(dataPath);
    bible_db.Open((dataPath + "/homein-bible.db").toStdString());
    lyrics_engine.Initialize((dataPath + "/homein-lyrics.db").toStdString());

    SetupUI();
}

HomeInDock::~HomeInDock() {
    StopTranscription();
}

void HomeInDock::SetupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    tab_widget = new QTabWidget(this);

    // 1. Instantiate the Tabs
    transcript_tab = new HomeInTranscriptTab(this);
    bible_tab      = new HomeInBibleTab(bible_db, this);
    lyrics_tab     = new HomeInLyricsTab(lyrics_engine, this);
    queue_tab      = new HomeInQueueTab(this);
    settings_tab   = new HomeInSettingsTab(this);

    // 2. Add them to the QTabWidget
    tab_widget->addTab(transcript_tab, "Transcript");
    tab_widget->addTab(bible_tab, "Bible");
    tab_widget->addTab(lyrics_tab, "Lyrics");
    tab_widget->addTab(queue_tab, "Queue");
    tab_widget->addTab(settings_tab, "Settings");
    main_layout->addWidget(tab_widget);

    // ---------------------------------------------------------
    // 3. THE WIRING: Connecting the isolated tabs together!
    // ---------------------------------------------------------

    // Routing Mic controls to the Dock
    connect(transcript_tab, &HomeInTranscriptTab::RequestToggleMic, this, &HomeInDock::StartTranscription);
    connect(transcript_tab, &HomeInTranscriptTab::RequestPauseMic, this, &HomeInDock::StopTranscription);

    // Routing Bible and Lyrics "Push to Overlay" clicks to the Renderer
    connect(bible_tab, &HomeInBibleTab::RequestPushToOverlay, this, &HomeInDock::PushToOverlay);
    connect(lyrics_tab, &HomeInLyricsTab::RequestPushToOverlay, this, &HomeInDock::PushToOverlay);
    connect(queue_tab, &HomeInQueueTab::RequestPushToOverlay, this, &HomeInDock::PushToOverlay);

    // Routing Bible and Lyrics "Add to Queue" clicks straight to the Queue tab
    connect(bible_tab, &HomeInBibleTab::RequestAddToQueue, queue_tab, &HomeInQueueTab::AddItemToQueue);
    connect(lyrics_tab, &HomeInLyricsTab::RequestAddToQueue, queue_tab, &HomeInQueueTab::AddItemToQueue);
}

void HomeInDock::StartTranscription() {
    if (is_running) return;
    is_running = true;
    
    GetAudioHandler()->Clear();
    transcript_queue->Clear();

    QString key = settings_tab->GetDeepgramKey();
    stt_provider = HomeInSTTManager::CreateEngine(ACTIVE_STT_ENGINE);

    // --- PREMIUM FEATURE: KEYWORD HARVESTING ---
    // We grab all niche vocabulary from the SQLite databases to bias the AI
    std::vector<std::string> boost_words;

    // 1. Harvest Bible Books
    auto books = bible_db.GetAllBooks();
    boost_words.insert(boost_words.end(), books.begin(), books.end());

    // 2. Harvest Song Titles and Artists
    auto songs = lyrics_engine.GetDB().GetLibrary(5000); // Pull up to 5000 local songs
    for (const auto& song : songs) {
        if (!song.title.empty()) boost_words.push_back(song.title);
        if (!song.artist.empty()) boost_words.push_back(song.artist);
    }

    // 3. Inject into the STT Engine (Only Deepgram will actively use this)
    stt_provider->SetKeywords(boost_words);
    // -------------------------------------------

    transcript_tab->SetAIStatus("Loading AI...", "#ffaa00");

    QtConcurrent::run([this, key]() {
        bool init_success = false;

        if (ACTIVE_STT_ENGINE == STTEngineType::DEEPGRAM_CLOUD) {
            init_success = stt_provider->Initialize(key.toStdString());
        } 
        else if (ACTIVE_STT_ENGINE == STTEngineType::WHISPER_LOCAL) {
            char* model_path = obs_module_file("models/ggml-base.en.bin");
            if (model_path) {
                init_success = stt_provider->Initialize(model_path);
                bfree(model_path);
            }
        } 
        else if (ACTIVE_STT_ENGINE == STTEngineType::VOSK_LOCAL) {
            char* model_path = obs_module_file("models/vosk-model-small-en-us-0.15");
            if (model_path) {
                init_success = stt_provider->Initialize(model_path);
                bfree(model_path);
            }
        }

        QMetaObject::invokeMethod(this, [this, init_success]() {
            if (!init_success) {
                transcript_tab->SetAIStatus("", "");
                QMessageBox::critical(this, "Speech Engine Error", "Failed to start AI Engine.");
                StopTranscription();
                return;
            }

            transcript_tab->SetAIStatus("● AI Live", "#2aee2a");
            
            stt_provider->Start([this](const std::string& text, bool is_partial) {
                if (!is_partial && !text.empty()) {
                    // Send to queue for the Background Detection Loop
                    transcript_queue->Push(text); 
                    
                    // Also send it straight to the UI
                    QMetaObject::invokeMethod(this, "HandleNewTranscript", Qt::QueuedConnection, Q_ARG(std::string, text));
                }
            });
        }, Qt::QueuedConnection);
    });
}

void HomeInDock::StopTranscription() {
    is_running = false;
    transcript_tab->SetAIStatus("", "");
    // Stop engine logic here...
}

void HomeInDock::HandleNewTranscript(const std::string& text) {
    // 1. Give text to the UI
    transcript_tab->AppendTranscriptText(QString::fromStdString(text));

    // 2. Send text to the Bible Parser
    auto verses = ref_parser.Parse(text);
    if (!verses.empty()) {
        const auto& best_match = verses.front();
        if (settings_tab->AutoSearch()) {
            bible_tab->AutoSearchVerse(QString::fromStdString(best_match.book), best_match.chapter, best_match.verse_start);
            if (settings_tab->AutoSwitchTabs()) {
                tab_widget->setCurrentWidget(bible_tab);
            }
        }
    }
}

void HomeInDock::PushToOverlay(const QString& text) {
    if (HomeInRenderer* renderer = GetActiveRenderer()) {
        renderer->SetText(text.toStdString());
    }
}