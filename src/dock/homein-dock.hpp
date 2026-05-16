#pragma once
#include <QWidget>
#include <QTabWidget>
#include <memory>
#include <thread>

#include "../stt/ISTTProvider.hpp"
#include "../stt/homein-transcript-queue.hpp"
#include "../detection/homein-context.hpp"
#include "../database/homein-db.hpp"
#include "../detection/homein-lyrics-engine.hpp"
#include "../detection/homein-ref-parser.hpp"

// Forward declare the isolated tabs
class HomeInTranscriptTab;
class HomeInBibleTab;
class HomeInLyricsTab;
class HomeInQueueTab;
class HomeInSettingsTab;

class HomeInDock : public QWidget {
    Q_OBJECT
public:
    explicit HomeInDock(QWidget *parent = nullptr);
    ~HomeInDock();

public slots:
    void PushToOverlay(const QString& text);

private slots:
    void StartTranscription();
    void StopTranscription();
    void HandleNewTranscript(const std::string& text);

private:
    void SetupUI();
    void DetectionLoop(); // Background thread for AI output scanning

    QTabWidget* tab_widget;

    // --- The 5 Isolated Tabs ---
    HomeInTranscriptTab* transcript_tab;
    HomeInBibleTab* bible_tab;
    HomeInLyricsTab* lyrics_tab;
    HomeInQueueTab* queue_tab;
    HomeInSettingsTab* settings_tab;

    // --- Core Engines ---
    HomeInDB bible_db;
    HomeInLyricsEngine lyrics_engine;
    HomeInRefParser ref_parser;
    std::shared_ptr<SermonContext> context;
    
    std::unique_ptr<ISTTProvider> stt_provider;
    std::shared_ptr<TranscriptQueue> transcript_queue;
    std::thread detection_thread;
    std::atomic<bool> is_running{false};
};