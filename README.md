# 🕊️ Home Indeed OBS Plugin

**Real-time AI-powered Bible Verse & Song Lyrics Overlay for OBS Studio.**

Home Indeed is a native C++ plugin designed for church media teams. It uses high-performance AI (Whisper.cpp) to "listen" to your live audio and automatically suggests relevant Bible verses or searches for worship song lyrics in real-time.

---

## ✨ Features

*   **🎙️ Real-time Transcription**: Powered by a local Whisper.cpp engine. No internet required for basic transcription.
*   **📖 Smart Bible Detection**: Automatically detects references like *"John 3:16"* or *"First Corinthians 13"* and suggests the text instantly from a built-in FTS5 database.
*   **🎶 Live Lyrics Engine**: 
    *   **Auto-Detection**: Identifying songs as they are sung.
    *   **LRCLIB Integration**: Live web-search for contemporary worship songs.
    *   **Local Caching**: Search your church's library offline.
*   **🖥️ HDMI Capture Card Support**: Works with any audio source, including HDMI feeds from video capture cards via OBS Audio Mixer filters.
*   **✨ Premium Overlays**: Native transparent video sources for high-quality, professional lower-thirds.
*   **📦 Zero Prerequisites**: Statically bundled dependencies for a simply "copy-and-paste" installation.

---

## 🚀 Quick Start

### 1. Installation (Recommended)
1.  Download the latest installer for your OS from the [Releases](https://github.com/obsproject/obs-plugintemplate/releases) page:
    *   **Windows**: `Home-Indeed-Installer.exe`
    *   **macOS**: `Home-Indeed-Installer.pkg`
    *   **Linux**: `Home-Indeed-Installer.deb`
2.  Run the installer. It will automatically detect your OBS installation and place the plugin, Bible databases, and AI models in the correct folders.
3.  Restart OBS.

### 2. Setup the Audio Tap
1.  Add your audio source (Microphone or Capture Card) to OBS.
2.  In the **Audio Mixer**, click the gear icon next to your source.
3.  Select **Filters** -> **+** -> **Home Indeed Audio Tap**.
4.  Open the **Home Indeed Control Dock** from the `Docks` menu.

### 3. Usage
*   **Bible**: Speak a reference or use the search bar. Click **Push to Screen** to display.
*   **Lyrics**: Use the Search tab or let the AI find the song for you. Move through verses with the **Previous/Next** stepper.

---

## 🛠️ Built-With

*   **Language**: C++20
*   **UI Framework**: Qt6 (OBS Frontend API)
*   **AI Engine**: [Whisper.cpp](https://github.com/ggerganov/whisper.cpp)
*   **Database**: SQLite 3 (FTS5)
*   **API Client**: LRCLIB (Lyrics Search)

---

## 🤝 Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for details on how to set up your development environment.

## 📄 License

This project is licensed under the **GPL v3.0 License** - see the [LICENSE](LICENSE) file for details.
