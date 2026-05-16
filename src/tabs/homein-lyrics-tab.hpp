#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include "../detection/homein-lyrics-engine.hpp"

class HomeInLyricsTab : public QWidget {
    Q_OBJECT

public:
    // Requires a reference to the Lyrics Engine (which holds the scraper & DB)
    explicit HomeInLyricsTab(HomeInLyricsEngine& engine, QWidget *parent = nullptr);

signals:
    // Same signals as the Bible Tab! The Main Dock handles them the exact same way.
    void RequestPushToOverlay(const QString& text);
    void RequestAddToQueue(const QString& display_text, const QString& payload, bool is_lyrics);

private:
    HomeInLyricsEngine& lyrics_engine;
    QLineEdit* search_box;
    QCheckBox* allow_web_checkbox;
    QListWidget* songs_list;
    QListWidget* sections_list;
    QPushButton* push_live_btn;
    QPushButton* add_queue_btn;

private slots:
    void OnSearchTriggered();
    void OnSongSelected();
    void OnPushLiveClicked();
    void OnQueueClicked();
};