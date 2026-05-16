#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include "../database/homein-db.hpp"

class HomeInBibleTab : public QWidget {
    Q_OBJECT

public:
    // Requires a reference to the loaded Bible DB
    explicit HomeInBibleTab(HomeInDB& db, QWidget *parent = nullptr);

    // Allows the Main Dock's Detection Loop to auto-search a verse
    void AutoSearchVerse(const QString& book, int chapter, int verse);

signals:
    // Shouts to the Main Dock to push text to the OBS Overlay
    void RequestPushToOverlay(const QString& text);
    // Shouts to the Main Dock to add an item to the Queue Tab
    void RequestAddToQueue(const QString& display_text, const QString& payload, bool is_lyrics);

private:
    HomeInDB& bible_db;
    QLineEdit* search_box;
    QListWidget* results_list;
    QPushButton* push_live_btn;
    QPushButton* add_queue_btn;
    QComboBox* translation_combo;

private slots:
    void OnSearchTriggered();
    void OnPushLiveClicked();
    void OnQueueClicked();
};