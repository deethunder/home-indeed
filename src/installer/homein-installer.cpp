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
    QCheckBox *desktopShortcut;
    QCheckBox *startMenuShortcut;
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
        titleLabel = new QLabel("Home Indeed Plugin Setup", this);
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

        // Options
        desktopShortcut = new QCheckBox("Create Desktop Shortcut to Data Folder", this);
        desktopShortcut->setChecked(true);
        startMenuShortcut = new QCheckBox("Add to Start Menu", this);
        startMenuShortcut->setChecked(true);
        layout->addWidget(desktopShortcut);
        layout->addWidget(startMenuShortcut);

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

    bool createLink(LPCWSTR lpszPathObj, LPCWSTR lpszPathLink, LPCWSTR lpszDesc) {
        HRESULT hres;
        IShellLink* psl;

        CoInitialize(NULL);
        hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
        if (SUCCEEDED(hres)) {
            psl->SetPath(lpszPathObj);
            psl->SetDescription(lpszDesc);

            IPersistFile* ppf;
            hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
            if (SUCCEEDED(hres)) {
                hres = ppf->Save(lpszPathLink, TRUE);
                ppf->Release();
            }
            psl->Release();
        }
        CoUninitialize();
        return SUCCEEDED(hres);
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

        // 2. Copy Data Folder
        statusLabel->setText("Copying data files...");
        QString baseDir = QCoreApplication::applicationDirPath();
        QString srcData = baseDir + "/data";
        
        // --- Development Search Logic ---
        // If not found in current dir, search up to 3 levels up (for build/RelWithDebInfo runs)
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
        
        // Verify source data exists
        if (!QDir(srcData).exists()) {
             QMessageBox::warning(this, "Setup Warning", 
                "Source 'data' folder not found. Some features (Bible/STT) may not work until databases are manually added.");
        } else {
            QString failedFile;
            if (!copyRecursively(srcData, dataPath, failedFile)) {
                QMessageBox::warning(this, "Partial Success", 
                    QString("Plugin copied, but file '%1' is currently in use or locked.\n\n"
                            "Please ensure all Bible/SQLite tools and OBS are closed.").arg(failedFile));
            }
        }
        
        // 3. Special check for Models
        QDir modelsDir(dataPath + "/models");
        if (modelsDir.entryList(QStringList() << "*.bin", QDir::Files).isEmpty()) {
            statusLabel->setText("Models missing - plugin will require manual model download.");
        }

        progressBar->setValue(80);

        // 3. Create Shortcuts
        if (desktopShortcut->isChecked() || startMenuShortcut->isChecked()) {
            statusLabel->setText("Creating shortcuts...");
            WCHAR desktopPath[MAX_PATH];
            SHGetSpecialFolderPathW(NULL, desktopPath, CSIDL_DESKTOPDIRECTORY, FALSE);
            
            std::wstring dataPathStr = QDir::toNativeSeparators(dataPath).toStdWString();
            
            if (desktopShortcut->isChecked()) {
                std::wstring obsExePath = QDir::toNativeSeparators(obsPath + "/bin/64bit/obs64.exe").toStdWString();
                std::wstring linkPath = std::wstring(desktopPath) + L"\\Home Indeed.lnk";
                createLink(obsExePath.c_str(), linkPath.c_str(), L"Home Indeed - Launch OBS with AI Overlay");
            }
            
            if (startMenuShortcut->isChecked()) {
                WCHAR startMenuPath[MAX_PATH];
                SHGetSpecialFolderPathW(NULL, startMenuPath, CSIDL_PROGRAMS, FALSE);
                std::wstring linkPath = std::wstring(startMenuPath) + L"\\Home Indeed.lnk";
                std::wstring obsExePath = QDir::toNativeSeparators(obsPath + "/bin/64bit/obs64.exe").toStdWString();
                createLink(obsExePath.c_str(), linkPath.c_str(), L"Home Indeed - Launch OBS with AI Overlay");

                // Uninstaller shortcut in Start Menu
                QString smFolder = QString::fromWCharArray(startMenuPath) + "/Home Indeed";
                QDir().mkpath(smFolder);
                QString uninstallerDest = dataPath + "/Home-Indeed-Uninstaller.exe";
                if (QFile::exists(uninstallerDest)) {
                    QFile::link(uninstallerDest, smFolder + "/Uninstall Home Indeed.lnk");
                }
            }
        }

        progressBar->setValue(100);
        statusLabel->setText("Installation Complete!");
        QMessageBox::information(this, "Success", "Home Indeed has been successfully installed.\n\nShortcuts have been created to your data folder.");
        
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
