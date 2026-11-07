# Desktop Orb

Windows desktop floating hub written in C++20. The first feature is a live, scrollable switcher for application windows across all monitors. See [REQUIREMENTS.md](REQUIREMENTS.md) for the product requirements.

## Build

Requires Visual Studio 2022 or newer with Desktop development with C++ and CMake.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Run `build\Release\desktop-orb.exe`. A floating ball appears at the edge of the primary display.
Only one instance runs across Windows login sessions. Launching the executable again exits without creating another ball.

## Controls

- Click the ball to show its feature menu; drag it to another edge or monitor.
- Click **窗口切换** to open the live window list on the same edge as the ball. The ball hides while the list is open and returns when it closes.
- Scroll the list, then click a window to activate it. `Esc` closes the menu or list.
- The default global shortcut is `Ctrl+Alt+Space`.
- Right-click the ball to open or reload the settings file, or exit.

Settings are stored at `%APPDATA%\DesktopOrb\settings.ini`. Change `Hotkey` to a combination such as `Ctrl+Shift+F8`, then choose **重新加载设置** from the ball's right-click menu. Supported main keys are A-Z, 0-9, Space and F1-F12.

## Source layout

- `app`: coordinates the orb, feature menu and switcher.
- `platform`: Windows monitors, settings, application windows and DWM thumbnails.
- `ui`: native window lifecycle, drawing and the three interface components.
