#pragma once
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class HomeInQueueTab : public QWidget {
    Q_OBJECT
public:
    explicit HomeInQueueTab(QWidget *parent = nullptr);

public slots:
    // The Main Dock will route data into this slot
    void AddItemToQueue(const QString& display_text, const QString& payload, bool is_lyrics);

signals:
    // Shouts to the Main Dock to render the selected item
    void RequestPushToOverlay(const QString& text);

private:
    QListWidget* queue_list;
    QPushButton* push_queue_btn;
    QPushButton* clear_queue_btn;
    QPushButton* up_queue_btn;
    QPushButton* down_queue_btn;

    void SaveQueue();
    void LoadQueue();

private slots:
    void OnPushClicked();
    void OnClearClicked();
    void OnMoveUpClicked();
    void OnMoveDownClicked();
};