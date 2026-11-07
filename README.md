# Desktop Orb

Windows desktop floating hub written in C++20. The first feature is a live, scrollable switcher for application windows across all monitors. See [REQUIREMENTS.md](REQUIREMENTS.md) for the product requirements.

## Build

Requires Visual Studio 2022 or newer with Desktop development with C++ and CMake.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Run `build\Release\desktop-orb.exe`. A floating ball appears at the edge of the primary display.
The build copies `assets/icons/*.png` to `build\Release\assets\icons\`. Keep the `assets` directory beside the exe when distributing or moving it.
Only one instance runs across Windows login sessions. Launching the executable again exits without creating another ball.

## Controls

- Click the ball to show its feature menu; drag it to another edge or monitor. Feature icons form a compact arc that expands as more features are added.
- Click the window switcher icon to open the live window list on the same edge as the ball. Its small transparent button grows while hovered; after 3 seconds, its name appears. The ball hides while the list is open and returns when it closes.
- Scroll the list, then click a window to activate it. `Esc` closes the menu or list.
- The default global shortcut is `Ctrl+Alt+Space`.
- Right-click the ball to open or reload the settings file, or exit.

Settings are stored at `%APPDATA%\DesktopOrb\settings.ini`. Change `Hotkey` to a combination such as `Ctrl+Shift+F8`, then choose **重新加载设置** from the ball's right-click menu. Supported main keys are A-Z, 0-9, Space and F1-F12.

## Source layout

- `app`: coordinates the orb, feature menu and switcher.
- `platform`: Windows monitors, settings, application windows and DWM thumbnails.
- `ui`: native window lifecycle, drawing and the three interface components.
- `assets/icons`: runtime PNG images. Add a lowercase, underscore-separated PNG here and build.

## Adding an icon

Put the final PNG in `assets/icons/`. In the UI object, keep an `IconImage` initialized with its filename, check `ready()`, then call `Draw(dc, bounds)` while painting. `OrbArtwork` uses `Rasterize()` for its layered window. Build again to copy the image beside the executable; no resource IDs or CMake edits are needed.
