#include "homein-bible-tab.hpp"
#include <QMessageBox>

HomeInBibleTab::HomeInBibleTab(HomeInDB& db, QWidget *parent) : QWidget(parent), bible_db(db) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Search Bar Area
    QHBoxLayout* search_layout = new QHBoxLayout();
    search_box = new QLineEdit();
    search_box->setPlaceholderText("e.g. John 3:16");
    QPushButton* search_btn = new QPushButton("Search");
    
    search_layout->addWidget(search_box);
    search_layout->addWidget(search_btn);
    layout->addLayout(search_layout);

    // Results List
    results_list = new QListWidget();
    results_list->setWordWrap(true);
    layout->addWidget(results_list);

    // Action Buttons
    QHBoxLayout* action_layout = new QHBoxLayout();
    push_live_btn = new QPushButton("Push Live");
    add_queue_btn = new QPushButton("Add to Queue");
    
    action_layout->addWidget(push_live_btn);
    action_layout->addWidget(add_queue_btn);
    layout->addLayout(action_layout);

    // Connections
    connect(search_btn, &QPushButton::clicked, this, &HomeInBibleTab::OnSearchTriggered);
    connect(search_box, &QLineEdit::returnPressed, this, &HomeInBibleTab::OnSearchTriggered);
    connect(push_live_btn, &QPushButton::clicked, this, &HomeInBibleTab::OnPushLiveClicked);
    connect(add_queue_btn, &QPushButton::clicked, this, &HomeInBibleTab::OnQueueClicked);
}

void HomeInBibleTab::OnSearchTriggered() {
    QString query = search_box->text().trimmed();
    if (query.isEmpty()) return;

    results_list->clear();
    auto verses = bible_db.SearchVerses(query.toStdString());
    
    for (const auto& v : verses) {
        QString display = QString("%1 %2:%3 - %4")
            .arg(QString::fromStdString(v.book_name))
            .arg(v.chapter)
            .arg(v.verse)
            .arg(QString::fromStdString(v.text));
            
        QListWidgetItem* item = new QListWidgetItem(display, results_list);
        item->setData(Qt::UserRole, display); // Save the raw text as payload
    }
}

void HomeInBibleTab::OnPushLiveClicked() {
    if (QListWidgetItem* item = results_list->currentItem()) {
        // Shout to the Main Dock to render this text!
        emit RequestPushToOverlay(item->data(Qt::UserRole).toString());
    }
}

void HomeInBibleTab::OnQueueClicked() {
    if (QListWidgetItem* item = results_list->currentItem()) {
        QString text = item->data(Qt::UserRole).toString();
        // Shout to the Main Dock to route this to the Queue Tab!
        emit RequestAddToQueue(text, text, false); 
    }
}

void HomeInBibleTab::AutoSearchVerse(const QString& book, int chapter, int verse) {
    search_box->setText(QString("%1 %2:%3").arg(book).arg(chapter).arg(verse));
    OnSearchTriggered();
}