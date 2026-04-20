# 🕊️ Home Indeed OBS Plugin (HI)

**Real-time AI-powered Bible Verse & Song Lyrics Overlay for OBS Studio.**

Home Indeed is a native C++ plugin designed for church media teams. It uses high-performance AI (Whisper.cpp) to "listen" to your live audio and automatically suggests relevant Bible verses or searches for worship song lyrics in real-time.

---

## ✨ Features

*   **🎙️ Real-time Transcription**: Powered by a local Whisper.cpp engine.
*   **📖 Smart Bible Detection**: Automatically detects references like *"John 3:16"* and suggests text instantly.
*   **🔊 Live Audio Test**: Use the built-in meter in the Settings tab to verify your microphone is picking up sound and transcribing correctly before your service starts.
*   **🎶 Scripture Queue**: Use the `+` button to build a list of upcoming scriptures for your service.
*   **⚙️ Professional Settings**: Customizable alignment, full-screen modes, and bible versions via a dedicated settings page.
*   **📦 One-Click Launcher**: Quick-launch OBS directly from your desktop.

---

## 🚀 Quick Start & Audio Setup

### 1. Installation
1.  Run the `Home-Indeed-Installer.exe`.
2.  It will create a **Home Indeed** shortcut on your desktop. Double-click it to launch OBS.

### 2. Step-by-Step Audio Activation (CRITICAL)
For the plugin to "hear" anything, you must tap an audio source:
1.  Add your audio source (Mic, HDMI Capture, etc.) to your OBS Scene.
2.  In the **Audio Mixer**, click the **Gear (⚙️)** or **three dots** next to your source.
3.  Select **Filters**.
4.  Click **+** -> **Home Indeed Audio Tap**.
5.  Open the **Home Indeed** dock from the `Docks` menu.

---

## ⚓ The Control Dock

### ⌨️ Bottom Toolbar
*   **`+` (Add)**: Adds the currently detected verse or lyric to your **Queue**.
*   **`🗑️` (Discard)**: Removes the selected item from your Queue.
*   **`⚙️` (Settings)**: Opens the settings page for alignment and audio help.
*   **`▲` / `▼`**: Navigates through your queued items.

### 📖 Usage
*   **Queue**: Double-click any item in the `Queue` tab to push it to the live overlay.
*   **Bible**: Speak a reference while using the Audio Tap. Click `+` to queue it or `Push` to show it immediately.

---

## 🛡️ Trust & Security

### Malware-Free Guarantee
We guarantee that the Home Indeed OBS Plugin is free of any malware. Source code is public for audit.

### Authenticity
Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

---

## 📄 License

This project is licensed under the **GPL v2.0 License** - see the [LICENSE](LICENSE) file for details.
