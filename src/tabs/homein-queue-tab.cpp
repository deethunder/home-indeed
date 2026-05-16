#include "homein-queue-tab.hpp"

HomeInQueueTab::HomeInQueueTab(QWidget *parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    queue_list = new QListWidget();
    layout->addWidget(queue_list);

    QHBoxLayout* controls = new QHBoxLayout();
    push_queue_btn = new QPushButton("Push Selected");
    clear_queue_btn = new QPushButton("Clear Queue");
    up_queue_btn = new QPushButton("↑");
    down_queue_btn = new QPushButton("↓");

    controls->addWidget(push_queue_btn);
    controls->addWidget(up_queue_btn);
    controls->addWidget(down_queue_btn);
    controls->addWidget(clear_queue_btn);
    layout->addLayout(controls);

    connect(push_queue_btn, &QPushButton::clicked, this, &HomeInQueueTab::OnPushClicked);
    connect(clear_queue_btn, &QPushButton::clicked, this, &HomeInQueueTab::OnClearClicked);
    connect(up_queue_btn, &QPushButton::clicked, this, &HomeInQueueTab::OnMoveUpClicked);
    connect(down_queue_btn, &QPushButton::clicked, this, &HomeInQueueTab::OnMoveDownClicked);
    connect(queue_list, &QListWidget::itemDoubleClicked, this, &HomeInQueueTab::OnPushClicked);
}

void HomeInQueueTab::AddItemToQueue(const QString& display_text, const QString& payload, bool is_lyrics) {
    QListWidgetItem* item = new QListWidgetItem(display_text, queue_list);
    item->setData(Qt::UserRole, payload);
    item->setData(Qt::UserRole + 1, is_lyrics);
    // You can set an icon here based on is_lyrics!
}

void HomeInQueueTab::OnPushClicked() {
    if (QListWidgetItem* item = queue_list->currentItem()) {
        emit RequestPushToOverlay(item->data(Qt::UserRole).toString());
    }
}

void HomeInQueueTab::OnClearClicked() {
    queue_list->clear();
}

void HomeInQueueTab::OnMoveUpClicked() {
    int row = queue_list->currentRow();
    if (row > 0) {
        queue_list->insertItem(row - 1, queue_list->takeItem(row));
        queue_list->setCurrentRow(row - 1);
    }
}

void HomeInQueueTab::OnMoveDownClicked() {
    int row = queue_list->currentRow();
    if (row >= 0 && row < queue_list->count() - 1) {
        queue_list->insertItem(row + 1, queue_list->takeItem(row));
        queue_list->setCurrentRow(row + 1);
    }
}