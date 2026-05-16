#pragma once
#include <QWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>

class HomeInTranscriptTab : public QWidget {
    Q_OBJECT

public:
    explicit HomeInTranscriptTab(QWidget *parent = nullptr);

    // Allows the Main Dock to inject text from the AI into this UI
    void AppendTranscriptText(const QString& text);
    void SetAIStatus(const QString& status, const QString& colorHex);

signals:
    // When the user clicks the Mic, it tells the Main Dock to turn on the AI
    void RequestToggleMic();
    void RequestPauseMic();

private:
    QPushButton* mic_btn;
    QPushButton* pause_btn;
    QLabel* ai_status_label;
    QProgressBar* audio_level_bar;
    QTextEdit* transcript_box;
    QLineEdit* live_preview_box;
};