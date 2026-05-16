#include "homein-settings-tab.hpp"
#include <QSettings>
#include <QPushButton>

HomeInSettingsTab::HomeInSettingsTab(QWidget *parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QGroupBox* ai_group = new QGroupBox("AI Engine");
    QVBoxLayout* ai_layout = new QVBoxLayout(ai_group);
    deepgram_key_edit = new QLineEdit();
    deepgram_key_edit->setPlaceholderText("Deepgram API Key (Optional)");
    deepgram_key_edit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    ai_layout->addWidget(deepgram_key_edit);
    layout->addWidget(ai_group);

    QGroupBox* auto_group = new QGroupBox("Automation");
    QVBoxLayout* auto_layout = new QVBoxLayout(auto_group);
    auto_search_checkbox = new QCheckBox("Auto-Search Bible Verses");
    auto_push_checkbox = new QCheckBox("Auto-Push Verses to Live Screen");
    auto_switch_tabs_checkbox = new QCheckBox("Auto-Switch to Bible Tab on Match");
    auto_layout->addWidget(auto_search_checkbox);
    auto_layout->addWidget(auto_push_checkbox);
    auto_layout->addWidget(auto_switch_tabs_checkbox);
    layout->addWidget(auto_group);

    // Save button
    QPushButton* save_btn = new QPushButton("Save Settings");
    layout->addWidget(save_btn);
    layout->addStretch();

    connect(save_btn, &QPushButton::clicked, this, &HomeInSettingsTab::SaveSettings);

    LoadSettings();
}

void HomeInSettingsTab::LoadSettings() {
    QSettings settings("HomeIndeed", "Plugin");
    deepgram_key_edit->setText(settings.value("deepgram_key", "").toString());
    auto_search_checkbox->setChecked(settings.value("auto_search", true).toBool());
    auto_push_checkbox->setChecked(settings.value("auto_push", true).toBool());
    auto_switch_tabs_checkbox->setChecked(settings.value("auto_switch", true).toBool());
}

void HomeInSettingsTab::SaveSettings() {
    QSettings settings("HomeIndeed", "Plugin");
    settings.setValue("deepgram_key", deepgram_key_edit->text());
    settings.setValue("auto_search", auto_search_checkbox->isChecked());
    settings.setValue("auto_push", auto_push_checkbox->isChecked());
    settings.setValue("auto_switch", auto_switch_tabs_checkbox->isChecked());
}

bool HomeInSettingsTab::AutoSearch() const { return auto_search_checkbox->isChecked(); }
bool HomeInSettingsTab::AutoPush() const { return auto_push_checkbox->isChecked(); }
bool HomeInSettingsTab::AutoSwitchTabs() const { return auto_switch_tabs_checkbox->isChecked(); }
QString HomeInSettingsTab::GetDeepgramKey() const { return deepgram_key_edit->text(); }