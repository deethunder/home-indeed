#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QPixmap>
#include <QIcon>
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>
#include <QtConcurrent/QtConcurrent>
#include "../network/homein-http.hpp"

#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#endif

class HomeIndeedInstaller : public QWidget {
    Q_OBJECT

public:
    HomeIndeedInstaller(QWidget *parent = nullptr) : QWidget(parent) {
        setupUI();
        detectObsPath();
    }

private:
    QLabel *titleLabel;
    QLabel *logoLabel;
    QLabel *statusLabel;
    QLineEdit *pathEdit;
    QProgressBar *progressBar;
    QPushButton *installButton;
    QString obsPath;

    void setupUI() {
        setWindowTitle("Home Indeed Setup - Powered by Deethunder Nexus");
        setFixedSize(500, 450);
        setWindowIcon(QIcon(":/assets/hi-logo.png"));

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(30, 30, 30, 30);
        layout->setSpacing(15);

        // Header layout with Logo on left
        QHBoxLayout *header_layout = new QHBoxLayout();
        logoLabel = new QLabel(this);
        QPixmap logo(":/assets/hi-logo.png");
        if (!logo.isNull()) {
            logoLabel->setPixmap(logo.scaled(140, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        logoLabel->setFixedWidth(150);
        logoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        header_layout->addWidget(logoLabel);

        QVBoxLayout *title_v_layout = new QVBoxLayout();
        titleLabel = new QLabel("Home Indeed", this);
        titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #0078D7;");
        title_v_layout->addWidget(titleLabel);

        QLabel *subTitle = new QLabel("Powered by Deethunder Nexus", this);
        subTitle->setStyleSheet("color: #666; font-size: 14px;");
        title_v_layout->addWidget(subTitle);
        title_v_layout->addStretch();
        
        header_layout->addLayout(title_v_layout);
        layout->addLayout(header_layout);

        // Path Selection
        QLabel *pathLabel = new QLabel("OBS Installation Path:", this);
        layout->addWidget(pathLabel);

        QHBoxLayout *pathLayout = new QHBoxLayout();
        pathEdit = new QLineEdit(this);
        QPushButton *browseButton = new QPushButton("Browse...", this);
        connect(browseButton, &QPushButton::clicked, this, &HomeIndeedInstaller::onBrowse);
        pathLayout->addWidget(pathEdit);
        pathLayout->addWidget(browseButton);
        layout->addLayout(pathLayout);

        // Progress
        progressBar = new QProgressBar(this);
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        layout->addWidget(progressBar);

        statusLabel = new QLabel("Ready to install", this);
        statusLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(statusLabel);

        // Install Button
        installButton = new QPushButton("Install Now", this);
        installButton->setMinimumHeight(40);
        installButton->setStyleSheet("background-color: #0078D7; color: white; font-weight: bold; border-radius: 5px;");
        connect(installButton, &QPushButton::clicked, this, &HomeIndeedInstaller::onInstall);
        layout->addWidget(installButton);
    }

    void detectObsPath() {
        // 1. Try Registry HKLM
        QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\OBS Studio", QSettings::NativeFormat);
        obsPath = settings.value("InstallDir").toString();

        // 2. Try Registry WOW6432Node
        if (obsPath.isEmpty()) {
            QSettings settings64("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\OBS Studio", QSettings::NativeFormat);
            obsPath = settings64.value("InstallDir").toString();
        }

        // 3. Fallback to default path
        if (obsPath.isEmpty()) {
            if (QDir("C:/Program Files/obs-studio").exists()) {
                obsPath = "C:/Program Files/obs-studio";
            }
        }

        pathEdit->setText(obsPath);
    }

    void onBrowse() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select OBS Studio Folder", obsPath);
        if (!dir.isEmpty()) {
            obsPath = dir;
            pathEdit->setText(obsPath);
        }
    }

    void onInstall() {
        obsPath = pathEdit->text();
        if (obsPath.isEmpty() || !QDir(obsPath).exists()) {
            QMessageBox::critical(this, "Error", "Invalid OBS Studio path selected.");
            return;
        }

        // Check if OBS is running
        if (isProcessRunning(L"obs64.exe")) {
            QMessageBox::warning(this, "OBS is Running", "Please close OBS Studio before continuing with the installation.");
            return;
        }

        installButton->setEnabled(false);
        statusLabel->setText("Installing...");
        progressBar->setValue(10);

        // Path variables
        QString binPath = obsPath + "/obs-plugins/64bit";
        QString dataPath = obsPath + "/data/obs-plugins/home-indeed";
        QString modelPath = dataPath + "/models";

        QDir().mkpath(binPath);
        QDir().mkpath(dataPath);
        QDir().mkpath(modelPath);

        // 1. Copy DLL & Uninstaller
        statusLabel->setText("Copying plugin files...");
        QString srcDll = QCoreApplication::applicationDirPath() + "/home-indeed.dll";
        QString srcUninstaller = QCoreApplication::applicationDirPath() + "/Home-Indeed-Uninstaller.exe";

        if (QFile::exists(srcDll)) {
            QFile::remove(binPath + "/home-indeed.dll");
            QFile::copy(srcDll, binPath + "/home-indeed.dll");
        }
        
        if (QFile::exists(srcUninstaller)) {
            QFile::remove(dataPath + "/Home-Indeed-Uninstaller.exe");
            QFile::copy(srcUninstaller, dataPath + "/Home-Indeed-Uninstaller.exe");
        }
        progressBar->setValue(40);

        // 2. Copy Data Folder (databases, models)
        statusLabel->setText("Copying data files...");
        QString baseDir = QCoreApplication::applicationDirPath();

        // Strategy 1: Look for a 'data' subfolder next to the exe
        QString srcData = baseDir + "/data";
        
        // Strategy 2: Walk up 3 levels (for build/Release runs)
        if (!QDir(srcData).exists()) {
            QDir parentDir(baseDir);
            for (int i = 0; i < 3; ++i) {
                if (parentDir.cdUp()) {
                    QString candidate = parentDir.absolutePath() + "/data";
                    if (QDir(candidate).exists()) {
                        srcData = candidate;
                        break;
                    }
                }
            }
        }

        if (!QDir(srcData).exists()) {
            QMessageBox::warning(this, "Setup Warning",
                "Source 'data' folder not found. Bible and Lyrics search will not work.\n\n"
                "Please manually copy the 'data' folder from the project to alongside the installer.");
        } else {
            QString failedFile;
            copyRecursively(srcData, dataPath, failedFile);
        }

        // --- Also copy any .db files sitting directly next to the installer ---
        QStringList dbFiles = {"homein-bible.db", "homein-lyrics.db"};
        for (const QString& dbFile : dbFiles) {
            QString srcDb = baseDir + "/" + dbFile;
            if (QFile::exists(srcDb)) {
                QFile::remove(dataPath + "/" + dbFile);
                QFile::copy(srcDb, dataPath + "/" + dbFile);
            }
        }
        
        // 3. Hardware-Aware AI Model Selection
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        int cores = sysInfo.dwNumberOfProcessors;
        
        // Detect NVIDIA GPU for "NVIDIA Boosted" Mode
        bool hasNvidia = false;
        QProcess checkGpu;
        checkGpu.start("cmd", QStringList() << "/c" << "wmic path win32_VideoController get name");
        if (checkGpu.waitForFinished(2000)) {
            if (checkGpu.readAllStandardOutput().toUpper().contains("NVIDIA")) {
                hasNvidia = true;
            }
        }
        
        QString modelName = "ggml-base.en.bin"; // Balanced Default
        QString modelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin";
        QString perfLevel = "Standard";

        // NVIDIA GPU always gets the "Small" High-Accuracy model
        if (hasNvidia || cores >= 8) {
            modelName = "ggml-small.en.bin"; // High Performance
            modelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin";
            perfLevel = hasNvidia ? "NVIDIA Boosted" : "High-Accuracy";
        } else if (cores < 4) {
            modelName = "ggml-tiny.en.bin";  // Lite Performance
            modelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin";
            perfLevel = "Ultra-Fast";
        }

        QString targetModel = modelPath + "/" + modelName;
        if (!QFile::exists(targetModel)) {
            statusLabel->setText(QString("Setting up AI Voice Engine (%1 mode)...").arg(perfLevel));
            progressBar->setValue(85);
            
            std::string url = modelUrl.toStdString();
            std::string target = targetModel.toStdString();
            
            // Background download with progress feedback
            QFuture<bool> future = QtConcurrent::run([url, target, this]() {
                return HomeIn::HttpClient::DownloadFile(url, target, [this](float p) {
                    QMetaObject::invokeMethod(this, [this, p]() {
                        // Map 0-100% download to 85-98% of overall progress
                        int val = 85 + (int)(p * 13);
                        progressBar->setValue(val);
                    }, Qt::QueuedConnection);
                });
            });

            while (!future.isFinished()) {
                QApplication::processEvents();
                QThread::msleep(50);
            }
        }

        progressBar->setValue(100);
        statusLabel->setText("Installation Complete!");
        QMessageBox::information(this, "Success", "Home Indeed has been successfully installed.\n\nYou can now find it inside OBS Studio under 'Tools' or the Dock menu.");
        
        if (QMessageBox::question(this, "Launch OBS", "Would you like to launch OBS Studio now?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            QProcess::startDetached(obsPath + "/bin/64bit/obs64.exe", QStringList(), obsPath + "/bin/64bit");
        }
        
        close();
    }

    bool copyRecursively(const QString &srcPath, const QString &dstPath, QString &failedFile) {
        QDir srcDir(srcPath);
        if (!srcDir.exists()) return false;
        QDir().mkpath(dstPath);

        foreach (QString file, srcDir.entryList(QDir::Files)) {
            QString srcFile = srcPath + "/" + file;
            QString dstFile = dstPath + "/" + file;
            
            // Attempt to remove with retry
            bool success = false;
            for (int i = 0; i < 3; ++i) {
                if (!QFile::exists(dstFile) || QFile::remove(dstFile)) {
                    if (QFile::copy(srcFile, dstFile)) {
                        success = true;
                        break;
                    }
                }
                Sleep(500); // Wait for potential file release
            }
            
            if (!success) {
                failedFile = file;
                return false;
            }
        }

        foreach (QString dir, srcDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!copyRecursively(srcPath + "/" + dir, dstPath + "/" + dir, failedFile)) return false;
        }

        return true;
    }

    bool isProcessRunning(const std::wstring &processName) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(PROCESSENTRY32W);

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (Process32FirstW(snapshot, &entry) == TRUE) {
            while (Process32NextW(snapshot, &entry) == TRUE) {
                if (std::wstring(entry.szExeFile) == processName) {
                    CloseHandle(snapshot);
                    return true;
                }
            }
        }
        CloseHandle(snapshot);
        return false;
    }
};

#include "homein-installer.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyle("Fusion"); // Modern look

    HomeIndeedInstaller installer;
    installer.show();

    return app.exec();
}
