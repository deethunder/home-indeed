#include "homein-wizard.hpp"
#include <QApplication>
#include <QStyle>
#include <QIcon>

HomeInWizard::HomeInWizard(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Home Indeed - AI Setup Guide");
    setFixedSize(450, 300);
    setStyleSheet("background-color: #202124; color: white; font-family: 'Segoe UI', sans-serif;");

    QVBoxLayout *main_layout = new QVBoxLayout(this);
    pages = new QStackedWidget(this);

    // --- Page 1: Engine Choice ---
    QWidget *p1 = new QWidget();
    QVBoxLayout *l1 = new QVBoxLayout(p1);
    
    QLabel *title1 = new QLabel("Choose Your Speech Engine", p1);
    title1->setStyleSheet("font-size: 18px; font-weight: bold; color: #8ab4f8;");
    l1->addWidget(title1);

    QLabel *desc1 = new QLabel("Home Indeed uses AI to hear your sermon. Select the engine that fits your needs:", p1);
    desc1->setWordWrap(true);
    l1->addWidget(desc1);

    whisper_radio = new QRadioButton("Whisper (Local & Private)", p1);
    whisper_radio->setChecked(true);
    whisper_radio->setStyleSheet("font-weight: bold; margin-top: 10px;");
    l1->addWidget(whisper_radio);
    l1->addWidget(new QLabel("  • Completely offline and free.\n  • Uses your computer's power.\n  • Best for privacy.", p1));

    deepgram_radio = new QRadioButton("Deepgram (Fast & Cloud)", p1);
    deepgram_radio->setStyleSheet("font-weight: bold; margin-top: 10px;");
    l1->addWidget(deepgram_radio);
    l1->addWidget(new QLabel("  • Near-instant transcription.\n  • Requires internet and API Key.\n  • Best for low-latency broadcasts.", p1));

    l1->addStretch();
    pages->addWidget(p1);

    // --- Page 2: API Key ---
    QWidget *p2 = new QWidget();
    QVBoxLayout *l2 = new QVBoxLayout(p2);

    QLabel *title2 = new QLabel("Enter Deepgram API Key", p2);
    title2->setStyleSheet("font-size: 18px; font-weight: bold; color: #8ab4f8;");
    l2->addWidget(title2);

    l2->addWidget(new QLabel("Deepgram requires an API key to work. You can get one for free at deepgram.com", p2));

    key_edit = new QLineEdit(p2);
    key_edit->setPlaceholderText("Paste your key here (e.g. 4a2b...)");
    key_edit->setEchoMode(QLineEdit::Password);
    key_edit->setStyleSheet("background: #1a1a1a; border: 1px solid #444; padding: 8px; border-radius: 4px; color: white;");
    l2->addWidget(key_edit);

    l2->addStretch();
    pages->addWidget(p2);

    main_layout->addWidget(pages);

    // Buttons
    QHBoxLayout *btn_layout = new QHBoxLayout();
    back_btn = new QPushButton("Back", this);
    back_btn->setVisible(false);
    
    next_btn = new QPushButton("Next", this);
    next_btn->setStyleSheet("background-color: #8ab4f8; color: #202124; font-weight: bold; padding: 8px 20px; border-radius: 4px;");

    btn_layout->addWidget(back_btn);
    btn_layout->addStretch();
    btn_layout->addWidget(next_btn);
    main_layout->addLayout(btn_layout);

    connect(next_btn, &QPushButton::clicked, this, &HomeInWizard::OnNext);
    connect(back_btn, &QPushButton::clicked, this, &HomeInWizard::OnBack);
}

void HomeInWizard::OnNext() {
    if (pages->currentIndex() == 0) {
        if (deepgram_radio->isChecked()) {
            pages->setCurrentIndex(1);
            back_btn->setVisible(true);
            next_btn->setText("Finish");
        } else {
            accept();
        }
    } else {
        accept();
    }
}

void HomeInWizard::OnBack() {
    pages->setCurrentIndex(0);
    back_btn->setVisible(false);
    next_btn->setText("Next");
}

QString HomeInWizard::GetSTTMode() const {
    return deepgram_radio->isChecked() ? "deepgram" : "whisper";
}

QString HomeInWizard::GetDeepgramKey() const {
    return key_edit->text().trimmed();
}
