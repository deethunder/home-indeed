# Home Indeed OBS Plugin

Home Indeed is a native C++ OBS plugin for church media teams. It keeps scripture, lyrics, and live audio workflows inside OBS Studio instead of splitting the service across separate tools.

Website: https://homeindeed.netlify.app/
Releases: https://github.com/DeeThunder/Home-Indeed/releases

## What it does

- Real-time transcription with Deepgram-backed flows
- Bible reference detection and scripture lookup
- Lyrics detection and queueing for live services
- A docked OBS interface for transcript, Bible, lyrics, queue, and settings views
- Audio tap and live meter support for checking whether OBS is hearing the source correctly

## Downloads

Each release publishes platform packages for:

- Windows: `home-indeed-<version>-windows-x64-Installer.exe`
- macOS: `home-indeed-<version>-macos-universal.pkg`
- Ubuntu/Linux: `home-indeed-<version>-x86_64-linux-gnu.deb`
- Ubuntu/Linux fallback archive: `home-indeed-<version>-x86_64-linux-gnu.zip`

## Install

1. Install OBS Studio first.
2. Download the package for your platform from the latest release.
3. Follow the steps in `INSTALL.md` if you want the full platform-specific install flow.
4. Restart OBS Studio after installation.

### Windows

Run the NSIS `.exe` installer.

### macOS

Open the `.pkg` installer. If macOS warns about unsigned software, approve the package in Privacy & Security.

### Ubuntu / Linux

Install the `.deb` package with:

```bash
sudo apt install ./home-indeed-<version>-x86_64-linux-gnu.deb
```

If you prefer an archive instead of a package manager install, use the `.zip` fallback from the release page.

## License
GPL v2.0. See [LICENSE](LICENSE).
