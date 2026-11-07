#include "desktop_orb/app.hpp"

#include "desktop_orb/platform/monitor.hpp"
#include <algorithm>
#include <shellapi.h>

namespace desktop_orb {
namespace {

constexpr int kHotkeyId = 1;

bool Contains(RECT rect, POINT point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

RECT Bounds(HWND window) {
    RECT result{};
    GetWindowRect(window, &result);
    return result;
}

void Warn(HWND owner, const wchar_t* title, const wchar_t* text) {
    MessageBoxW(owner, text, title, MB_OK | MB_ICONWARNING);
}

}  // namespace

App::App() : panel_(catalog_) {}

App::~App() {
    if (hotkey_registered_) UnregisterHotKey(orb_.handle(), kHotkeyId);
}

bool App::Initialize() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    settings_ = settings_store_.Load();
    const auto primary = platform::MonitorAt({0, 0});
    const POINT initial = settings_.orb_position.value_or(POINT{
        primary.work.right - 58, primary.work.top + (primary.work.bottom - primary.work.top) / 3});
    if (!orb_.Create(initial) || !panel_.Create()) return false;
    orb_.SnapToEdge();
    if (!menu_.Add(L"窗口切换", [this] { OpenSwitcher(); })) return false;
    auto excluded = menu_.Handles();
    excluded.push_back(orb_.handle());
    excluded.push_back(panel_.handle());
    panel_.SetExcluded(std::move(excluded));

    orb_.on_click = [this] { ToggleMenu(); };
    orb_.on_drag_start = [this] { CloseOverlays(); };
    orb_.on_moved = [this](POINT position) { SavePosition(position); };
    orb_.on_tick = [this] { Tick(); };
    orb_.on_hotkey = [this] { ToggleMenu(); };
    orb_.on_context = [this] { ShowContextMenu(); };

    if (!settings_store_.Save(settings_))
        Warn(orb_.handle(), L"设置保存失败", L"无法写入设置文件，悬浮球位置可能无法保留。");
    const auto hotkey = platform::SettingsStore::ParseHotkey(settings_.hotkey);
    if (!hotkey || !RegisterShortcut(*hotkey))
        Warn(orb_.handle(), L"快捷键不可用", L"无法注册当前快捷键。可右键悬浮球修改设置后重新加载。");
    return true;
}

int App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool App::RegisterShortcut(const platform::Hotkey& hotkey) {
    hotkey_registered_ = RegisterHotKey(orb_.handle(), kHotkeyId, hotkey.modifiers, hotkey.key) != FALSE;
    return hotkey_registered_;
}

void App::SavePosition(POINT position) {
    settings_.orb_position = position;
    settings_store_.Save(settings_);
}

void App::Tick() {
    catalog_.ObserveForeground();
    POINT cursor{};
    GetCursorPos(&cursor);
    const RECT orb_bounds = orb_.Bounds();
    const RECT hover_bounds{orb_bounds.left - 28, orb_bounds.top - 28,
                            orb_bounds.right + 28, orb_bounds.bottom + 28};
    orb_.SetAlpha(Contains(hover_bounds, cursor) || menu_.visible() || panel_.visible() ? 255 : 125);

    const bool left_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (left_down && !previous_left_down_ && (menu_.visible() || panel_.visible())) {
        bool inside = Contains(orb_bounds, cursor);
        if (panel_.visible()) inside = inside || Contains(Bounds(panel_.handle()), cursor);
        if (menu_.visible()) {
            for (HWND button : menu_.Handles()) inside = inside || Contains(Bounds(button), cursor);
        }
        if (!inside) CloseOverlays();
    }
    previous_left_down_ = left_down;

    const bool escape_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escape_down && !previous_escape_down_) CloseOverlays();
    previous_escape_down_ = escape_down;
}

void App::ToggleMenu() {
    if (menu_.visible()) { menu_.Hide(); return; }
    panel_.Hide();
    const RECT bounds = orb_.Bounds();
    const POINT center{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
    menu_.ShowAround(bounds, platform::MonitorAt(center), GetDpiForWindow(orb_.handle()));
}

void App::OpenSwitcher() {
    menu_.Hide();
    const RECT bounds = orb_.Bounds();
    const POINT center{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
    panel_.ShowFor(platform::MonitorAt(center));
    // Keep the orb reachable when it is attached to the same left edge as the panel.
    SetWindowPos(orb_.handle(), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void App::CloseOverlays() {
    menu_.Hide();
    panel_.Hide();
}

void App::ReloadSettings() {
    const auto next = settings_store_.Load();
    const auto hotkey = platform::SettingsStore::ParseHotkey(next.hotkey);
    if (!hotkey) {
        Warn(orb_.handle(), L"快捷键无效", L"请使用 Ctrl、Alt、Shift 或 Win，加上字母、数字、Space 或 F1-F12。");
        return;
    }
    const auto previous = platform::SettingsStore::ParseHotkey(settings_.hotkey);
    if (hotkey_registered_) UnregisterHotKey(orb_.handle(), kHotkeyId);
    hotkey_registered_ = false;
    if (RegisterShortcut(*hotkey)) {
        settings_.hotkey = next.hotkey;
    } else {
        if (previous) RegisterShortcut(*previous);
        Warn(orb_.handle(), L"快捷键不可用", L"该组合可能已被其他程序占用，请修改后重试。");
    }
}

void App::ShowContextMenu() {
    CloseOverlays();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"打开设置文件");
    AppendMenuW(menu, MF_STRING, 2, L"重新加载设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"退出");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(orb_.handle());
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       cursor.x, cursor.y, 0, orb_.handle(), nullptr);
    DestroyMenu(menu);
    switch (command) {
        case 1:
            ShellExecuteW(orb_.handle(), L"open", L"notepad.exe", settings_store_.path().c_str(),
                          nullptr, SW_SHOWNORMAL);
            break;
        case 2: ReloadSettings(); break;
        case 3: DestroyWindow(orb_.handle()); break;
        default: break;
    }
}

}  // namespace desktop_orb
