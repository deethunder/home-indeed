# Home Indeed Installation

Home Indeed is an OBS Studio plugin. Install OBS Studio first, then use the installer for your operating system.

## Windows

Run `home-indeed-<version>-windows-x64-Installer.exe` and follow the prompts.

The installer places the plugin files under the OBS Studio plugin directory. Restart OBS Studio after installation.

## macOS

Open `home-indeed-<version>-macos-universal.pkg` and follow the installer prompts.

If macOS blocks the package because it is unsigned, open System Settings, go to Privacy & Security, and allow the package from there. Restart OBS Studio after installation.

## Ubuntu

Install the Debian package with:

```bash
sudo apt install ./home-indeed-<version>-x86_64-linux-gnu.deb
```

Restart OBS Studio after installation.

## Verify

Open OBS Studio and check that Home Indeed loads in the OBS interface. If OBS was open during installation, close and reopen it.
