# ShotX

Fast region screenshots with an annotation editor, for KDE Plasma 6.

ShotX runs in the system tray. By default, captures are copied to the clipboard and saved to `~/Pictures/Screenshots/`. Both can be changed in the settings.

## Requirements

- KDE Plasma 6 (captures go through KWin's `org.kde.KWin.ScreenShot2` interface)
- CMake 3.20+ and a C++20 compiler
- Qt6 (Widgets, DBus), KF6GlobalAccel, LayerShellQt

On Ubuntu:

```sh
sudo apt install cmake g++ qt6-base-dev libkf6globalaccel-dev liblayershellqtinterface-dev
```

## Install

### Prebuilt packages

Each [release](https://github.com/JanPlazovnik/ShotX/releases) has a `.deb` built on Ubuntu 26.04 and an `.rpm` built on the latest Fedora.

```sh
sudo apt install ./shotx_*.deb        # Ubuntu / Debian
sudo dnf install ./shotx-*.rpm        # Fedora
rpm-ostree install ./shotx-*.rpm      # Fedora Atomic / Bazzite, then reboot
```

To publish a release, push a version tag; CI builds both packages and attaches them:

```sh
git tag v0.1.0 && git push origin v0.1.0
```

### .deb package (Ubuntu / Debian)

```sh
cmake -S . -B build-pkg -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-pkg -j"$(nproc)"
cd build-pkg && cpack
sudo apt install ./shotx_*.deb
```

This installs:

| File | Purpose |
|---|---|
| `/usr/bin/shotx` | The binary |
| `/usr/share/applications/shotx.desktop` | Launcher entry; KWin uses it to allow ShotX to take screenshots |
| `/etc/xdg/autostart/shotx.desktop` | Starts ShotX on login (can be turned off in settings) |

Remove it with `sudo apt remove shotx`.

### Per-user install (no root)

```sh
./install.sh
```

This builds the binary and installs it to `~/.local/bin/shotx`, with the launcher and autostart entries in `~/.local/share/applications` and `~/.config/autostart`. Run it again to update.

Don't combine the two methods. The per-user autostart entry overrides the system one, so the copy in `~/.local/bin` would be the one that runs on login.

After installing, start ShotX from the app menu or run `shotx &`, or log out and back in.

## Settings

Open the settings from the tray menu (**Configure ShotX…**), by launching ShotX from the app menu while it is already running, or with `shotx settings`.

- **Saving:** folder, file name pattern and image format (PNG, JPG, WebP). The file name uses Qt date/time format (`yyyy MM dd HH mm ss zzz`). Put literal text in single quotes, e.g. `'Screenshot_'yyyy-MM-dd_HH-mm-ss-zzz`.
- **After capture:** copy to clipboard, save to folder, notify when saved.
- **Startup:** start ShotX on login.
- **Shortcuts:** a primary and alternate key for each capture action.

Settings are stored in `~/.config/shotx/shotx.conf`. Shortcuts are stored by KDE's global shortcut service, so they also appear in System Settings → Keyboard → Shortcuts.

## Make ShotX the default screenshot tool

Default shortcuts:

| Shortcut | Action |
|---|---|
| `Print`, `Meta+Shift+S` | Capture region |
| `Ctrl+Print` | Capture region and edit |
| `Shift+Print` | Capture current screen |
| `Meta+Print` | Capture active window |

If another app (such as Spectacle) already owns these keys, ShotX won't get them. To take them over:

1. Back up your current shortcuts:

   ```sh
   cp ~/.config/kglobalshortcutsrc ~/.config/kglobalshortcutsrc.before-shotx-takeover
   ```

2. In ShotX settings, open **Shortcuts**, click **Use default shortcuts**, then **OK**. ShotX asks before taking each key from another app.

   Or, from a terminal, take all default keys without asking:

   ```sh
   shotx take-shortcuts
   ```

To undo, reassign the keys in System Settings → Keyboard → Shortcuts. The backup file shows what they were before.

## Usage

```sh
shotx [region|region-edit|screen|window|take-shortcuts|settings]
```

With no argument, ShotX starts in the tray, or opens the settings if it is already running. With an argument, it sends the command to the running instance, or starts one and runs it.

Left-clicking the tray icon captures a region. The tray menu also opens the screenshots folder and the settings.
