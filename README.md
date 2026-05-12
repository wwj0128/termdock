# TermDock

TermDock（终端舱）is a lightweight Windows terminal session manager built with Qt and Windows ConPTY.

It focuses on managing multiple CMD sessions in a single, polished desktop interface while keeping terminal input responsive.

## Features

- Manage multiple CMD sessions from one window
- Switch sessions with a readable live/saved directory list
- Save working directories and reopen them quickly
- Inline rename/delete management for saved shortcuts
- ConPTY-based terminal interaction on Windows 10/11
- ANSI color rendering, scrollback, selection copy, and paste support
- Window icon and system tray integration
- Close-to-tray behavior with quick restore and new-session actions
- Convenience command shortcuts such as `cd...` for moving up multiple parent directories

## Download

Download the latest Windows package from GitHub Releases, unzip it, and run `TermDock.exe`.

## Requirements

- Windows 10 1809 or newer, Windows 11 recommended
- CMake 3.20+
- Visual Studio C++ toolchain
- Qt Widgets, Qt 5.14+ or Qt 6.x

## Build

Configure and build with CMake:

```powershell
cmake -S . -B build-win32 -G "Visual Studio 17 2022" -A Win32 -DCMAKE_PREFIX_PATH="D:/software/qt/5.14.2/msvc2017"
cmake --build build-win32 --config Release
```

Deploy Qt runtime files if needed:

```powershell
D:/software/qt/5.14.2/msvc2017/bin/windeployqt.exe build-win32/Release/TermDock.exe
```

Adjust the Qt path for your local installation.

## Notes

TermDock currently targets Windows CMD through ConPTY. Other shells may work in the future, but CMD compatibility and responsiveness are the current priority.

## License

MIT License. See [LICENSE](LICENSE).
