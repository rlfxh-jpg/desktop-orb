# Desktop Orb

Windows desktop floating hub written in C++20. The first feature is a live, scrollable switcher for application windows across all monitors. See [REQUIREMENTS.md](REQUIREMENTS.md) for the product requirements.

## Build

Requires Visual Studio 2022 or newer with Desktop development with C++ and CMake. Screenshot analysis uses Windows WinHTTP and CryptoAPI; it has no Python or third-party HTTP library dependency.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Screenshot text extraction and summarization call an OpenAI-compatible vision model directly from C++. All application settings are stored together in `settings.ini` beside the running executable. Right-click the floating ball and choose **打开设置文件** to edit it, then choose **重新加载设置** to apply changes:

```ini
[Controls]
Hotkey=Ctrl+Alt+Space

[Orb]
X=1200
Y=300

[ScreenshotAI]
ApiKey=your-api-key
BaseUrl=https://api.openai.com/v1
Model=gpt-4o-mini
```

`ApiKey` is stored as plain text in this file. For a packaged build, `settings.ini` is created beside `desktop-orb.exe` on first launch; you can also place it there before launch. `BaseUrl` is the service root; the client appends `/chat/completions` automatically. Remote services must use HTTPS; HTTP is accepted for loopback development servers. The client uses Windows certificate validation and the system proxy settings. HTTP 429 errors now distinguish quota or billing exhaustion from request rate limits and display the provider's error message.

Run `build\Release\desktop-orb.exe`. A floating ball appears at the edge of the primary display.
The build copies `assets/icons/*.png` to `build\Release\assets\icons\`. Keep the `assets` directory beside the exe when distributing or moving it.
Only one instance runs across Windows login sessions. Launching the executable again exits without creating another ball.

## Controls

- Click the ball to show its feature menu; drag it to another edge or monitor. Feature icons form a compact arc that expands as more features are added.
- Click the window switcher icon to open the live window list on the same edge as the ball. Its small transparent button grows while hovered; after 3 seconds, its name appears. The ball hides while the list is open and returns when it closes.
- Click the screenshot icon, then drag across the frozen desktop to select an area. The live selection size is shown in pixels. Choose **提取文字**, **总结截图**, **复制**, **保存**, **重新截取**, or **关闭** from the toolbar. AI results appear below the toolbar in a selectable, scrollable panel; click **复制结果** or select text and press `Ctrl+C`. Saving opens a folder picker. `Esc` closes the selection, and `Enter` copies the selected screenshot.
- Scroll the list, then click a window to activate it. `Esc` closes the menu or list.
- The default global shortcut is `Ctrl+Alt+Space`.
- Right-click the ball to open or reload the settings file, or exit.

Change `Hotkey` in the same `settings.ini` to a combination such as `Ctrl+Shift+F8`, then choose **重新加载设置** from the ball's right-click menu. Supported main keys are A-Z, 0-9, Space and F1-F12.

## Source layout

- `app`: composes shared UI and feature modules.
- `platform/common`: shared monitor, settings, asset path and single-instance support.
- `platform/window_switcher`: window discovery, activation and DWM thumbnails.
- `platform/screenshot`: virtual desktop capture, PNG output, clipboard integration and the native C++ WinHTTP vision client.
- `ui/screenshot`: full virtual desktop selection overlay, size feedback and post-capture actions.
- `ui/common`: floating orb, shared window lifecycle, drawing, icon loading and feature menu.
- `ui/window_switcher`: the window switcher panel and its interactions.
- `assets/icons`: runtime PNG images. Add a lowercase, underscore-separated PNG here and build.

## Adding an icon

Put the final PNG in `assets/icons/`. In the UI object, keep an `IconImage` initialized with its filename, check `ready()`, then call `Draw(dc, bounds)` while painting. `OrbArtwork` uses `Rasterize()` for its layered window. Build again to copy the image beside the executable; no resource IDs or CMake edits are needed.
