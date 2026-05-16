#include "homein-transcript-tab.hpp"

HomeInTranscriptTab::HomeInTranscriptTab(QWidget *parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Toolbar
    QHBoxLayout* toolbar = new QHBoxLayout();
    mic_btn = new QPushButton(" Start Mic");
    pause_btn = new QPushButton(" Pause");
    pause_btn->setEnabled(false);
    ai_status_label = new QLabel("", this);

    toolbar->addWidget(mic_btn);
    toolbar->addWidget(pause_btn);
    toolbar->addWidget(ai_status_label);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    // Audio Level Bar
    audio_level_bar = new QProgressBar();
    audio_level_bar->setMaximumHeight(5);
    audio_level_bar->setTextVisible(false);
    layout->addWidget(audio_level_bar);

    // Live AI Preview
    live_preview_box = new QLineEdit();
    live_preview_box->setReadOnly(true);
    live_preview_box->setPlaceholderText("Live Audio Test...");
    layout->addWidget(live_preview_box);

    // Final Transcript Box
    transcript_box = new QTextEdit();
    transcript_box->setReadOnly(true);
    layout->addWidget(transcript_box);

    // Wire Buttons to Signals
    connect(mic_btn, &QPushButton::clicked, this, &HomeInTranscriptTab::RequestToggleMic);
    connect(pause_btn, &QPushButton::clicked, this, &HomeInTranscriptTab::RequestPauseMic);
}

void HomeInTranscriptTab::AppendTranscriptText(const QString& text) {
    transcript_box->append(text);
    live_preview_box->clear(); // Clear the partial box when a full sentence finishes
}

void HomeInTranscriptTab::SetAIStatus(const QString& status, const QString& colorHex) {
    if (status.isEmpty()) {
        ai_status_label->setText("");
    } else {
        ai_status_label->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 11px;").arg(colorHex));
        ai_status_label->setText(status);
    }
}