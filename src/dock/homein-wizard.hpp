#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QButtonGroup>

class HomeInWizard : public QDialog {
    Q_OBJECT

public:
    explicit HomeInWizard(QWidget *parent = nullptr);

    QString GetSTTMode() const;
    QString GetDeepgramKey() const;

private slots:
    void OnNext();
    void OnBack();

private:
    QStackedWidget *pages;
    
    // Page 1: Selection
    QRadioButton *whisper_radio;
    QRadioButton *deepgram_radio;
    
    // Page 2: Key
    QLineEdit *key_edit;
    
    QPushButton *next_btn;
    QPushButton *back_btn;
};
