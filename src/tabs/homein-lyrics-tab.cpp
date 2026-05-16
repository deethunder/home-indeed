#include "homein-lyrics-tab.hpp"
#include <QMessageBox>
#include <QSplitter>

HomeInLyricsTab::HomeInLyricsTab(HomeInLyricsEngine& engine, QWidget *parent) 
    : QWidget(parent), lyrics_engine(engine) {
    
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Search Bar
    QHBoxLayout* search_layout = new QHBoxLayout();
    search_box = new QLineEdit();
    search_box->setPlaceholderText("Search song or artist...");
    QPushButton* search_btn = new QPushButton("Search");
    allow_web_checkbox = new QCheckBox("Allow Web Search");
    allow_web_checkbox->setChecked(true);
    
    search_layout->addWidget(search_box);
    search_layout->addWidget(allow_web_checkbox);
    search_layout->addWidget(search_btn);
    layout->addLayout(search_layout);

    // Splitter for Songs and Sections
    QSplitter* splitter = new QSplitter(Qt::Vertical);
    
    songs_list = new QListWidget();
    sections_list = new QListWidget();
    sections_list->setWordWrap(true);
    
    splitter->addWidget(songs_list);
    splitter->addWidget(sections_list);
    layout->addWidget(splitter);

    // Action Buttons
    QHBoxLayout* action_layout = new QHBoxLayout();
    push_live_btn = new QPushButton("Push Live");
    add_queue_btn = new QPushButton("Add to Queue");
    
    action_layout->addWidget(push_live_btn);
    action_layout->addWidget(add_queue_btn);
    layout->addLayout(action_layout);

    // Connections
    connect(search_btn, &QPushButton::clicked, this, &HomeInLyricsTab::OnSearchTriggered);
    connect(search_box, &QLineEdit::returnPressed, this, &HomeInLyricsTab::OnSearchTriggered);
    connect(songs_list, &QListWidget::itemSelectionChanged, this, &HomeInLyricsTab::OnSongSelected);
    connect(push_live_btn, &QPushButton::clicked, this, &HomeInLyricsTab::OnPushLiveClicked);
    connect(add_queue_btn, &QPushButton::clicked, this, &HomeInLyricsTab::OnQueueClicked);
}

void HomeInLyricsTab::OnSearchTriggered() {
    QString query = search_box->text().trimmed();
    if (query.isEmpty()) return;

    songs_list->clear();
    sections_list->clear();
    songs_list->addItem("Searching...");

    lyrics_engine.Search(query.toStdString(), allow_web_checkbox->isChecked(), 
        [this](const std::vector<SongLyric>& results) {
            songs_list->clear();
            if (results.empty()) {
                songs_list->addItem("No results found.");
                return;
            }
            for (const auto& song : results) {
                QString display = QString("%1 - %2").arg(QString::fromStdString(song.title), QString::fromStdString(song.artist));
                QListWidgetItem* item = new QListWidgetItem(display, songs_list);
                item->setData(Qt::UserRole, QString::fromStdString(song.content)); // Save full lyrics
            }
        });
}

void HomeInLyricsTab::OnSongSelected() {
    sections_list->clear();
    if (QListWidgetItem* item = songs_list->currentItem()) {
        QString full_lyrics = item->data(Qt::UserRole).toString();
        QStringList lines = full_lyrics.split('\n', Qt::SkipEmptyParts);
        
        // Group by 2 or 4 lines (simplified here)
        QString current_block = "";
        for (int i = 0; i < lines.size(); ++i) {
            current_block += lines[i] + "\n";
            if ((i + 1) % 4 == 0 || i == lines.size() - 1) {
                QListWidgetItem* sec_item = new QListWidgetItem(current_block.trimmed(), sections_list);
                sec_item->setData(Qt::UserRole, "\x01" + current_block.trimmed()); // \x01 marks it as lyrics for the renderer
                current_block = "";
            }
        }
    }
}

void HomeInLyricsTab::OnPushLiveClicked() {
    if (QListWidgetItem* item = sections_list->currentItem()) {
        emit RequestPushToOverlay(item->data(Qt::UserRole).toString());
    }
}

void HomeInLyricsTab::OnQueueClicked() {
    if (QListWidgetItem* item = sections_list->currentItem()) {
        QString payload = item->data(Qt::UserRole).toString();
        // Extract a short preview for the queue display
        QString display = payload;
        display.remove("\x01");
        display = display.left(30).replace('\n', ' ') + "...";
        emit RequestAddToQueue(display, payload, true); 
    }
}