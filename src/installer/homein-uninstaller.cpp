#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QProcess>
#include <QMessageBox>
#include <QIcon>

class HomeInUninstaller : public QWidget {
public:
    HomeInUninstaller(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowTitle("Home Indeed Uninstaller");
        setFixedSize(400, 200);
        
        QVBoxLayout *layout = new QVBoxLayout(this);
        
        QLabel *title = new QLabel("Uninstall Home Indeed Plugin", this);
        title->setStyleSheet("font-size: 16px; font-weight: bold;");
        layout->addWidget(title);
        
        QLabel *desc = new QLabel("This will remove all plugin files, models, and databases from your OBS installation.", this);
        desc->setWordWrap(true);
        layout->addWidget(desc);
        
        progressBar = new QProgressBar(this);
        progressBar->setRange(0, 100);
        layout->addWidget(progressBar);
        
        statusLabel = new QLabel("Ready to uninstall.", this);
        layout->addWidget(statusLabel);
        
        uninstallBtn = new QPushButton("Uninstall Now", this);
        uninstallBtn->setStyleSheet("background-color: #d32f2f; color: white; padding: 8px; font-weight: bold;");
        connect(uninstallBtn, &QPushButton::clicked, this, &HomeInUninstaller::onUninstall);
        layout->addWidget(uninstallBtn);
    }

private:
    QProgressBar *progressBar;
    QLabel *statusLabel;
    QPushButton *uninstallBtn;

    void onUninstall() {
        // 1. Check for Running OBS
        QProcess checkObs;
        checkObs.start("tasklist", QStringList() << "/FI" << "IMAGENAME eq obs64.exe");
        checkObs.waitForFinished();
        if (checkObs.readAllStandardOutput().contains("obs64.exe")) {
            QMessageBox::warning(this, "OBS Running", "Please close OBS Studio before uninstalling Home Indeed.");
            return;
        }

        uninstallBtn->setEnabled(false);
        progressBar->setValue(10);
        statusLabel->setText("Locating OBS Folder...");

        // 2. Identify OBS Folder (same logic as installer)
        QString obsPath = "C:/Program Files/obs-studio";
        QSettings reg("HKEY_LOCAL_MACHINE\\SOFTWARE\\OBS Studio", QSettings::NativeFormat);
        if (reg.contains("InstallDir")) {
            obsPath = reg.value("InstallDir").toString();
        }

        progressBar->setValue(30);
        statusLabel->setText("Deleting plugin files...");

        // 3. Remove Files
        bool success = true;
        
        // Remove DLL
        QString dllPath = obsPath + "/obs-plugins/64bit/home-indeed.dll";
        if (QFile::exists(dllPath)) {
            if (!QFile::remove(dllPath)) success = false;
        }
        
        progressBar->setValue(60);
        statusLabel->setText("Deleting data folder...");

        // Remove Data Folder (Recursive)
        QString dataPath = obsPath + "/data/obs-plugins/home-indeed";
        if (QDir(dataPath).exists()) {
            QDir dir(dataPath);
            if (!dir.removeRecursively()) success = false;
        }

        progressBar->setValue(80);
        statusLabel->setText("Removing Shortcuts...");

        // Remove Start Menu
        QString smPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Home Indeed";
        if (QDir(smPath).exists()) {
            QDir(smPath).removeRecursively();
        }

        progressBar->setValue(100);
        if (success) {
            statusLabel->setText("Uninstall Complete!");
            QMessageBox::information(this, "Success", "Home Indeed has been successfully removed.");
            close();
        } else {
            statusLabel->setText("Partial failure. Some files may be locked.");
            QMessageBox::warning(this, "Warning", "Some files could not be removed. Please delete the 'home-indeed' folder manually in OBS data directory if residue remains.");
        }
    }
};

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    HomeInUninstaller w;
    w.show();
    return a.exec();
}
