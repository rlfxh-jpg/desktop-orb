// 文件职责：协调悬浮球、功能菜单和窗口面板的状态转换。
// UI 组件由 App 持有；平台服务负责窗口枚举、设置和显示器信息。
#include "desktop_orb/app.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <shellapi.h>
#include <shobjidl.h>
#include <utility>
#include <wrl/client.h>

namespace desktop_orb {
namespace {

constexpr int kHotkeyId = 1;

// 作用：判断屏幕点是否落在矩形内部。
// 参数：rect 为屏幕矩形；point 为屏幕点。返回：在矩形内为 true，否则为 false。
bool Contains(RECT rect, POINT point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

// 作用：读取窗口屏幕矩形，供面板外点击判断。
// 参数：window 为目标 HWND。返回：窗口矩形；系统调用失败时为零矩形。
RECT Bounds(HWND window) {
    RECT result{};
    GetWindowRect(window, &result);
    return result;
}

// 作用：显示不阻断应用继续运行的警告。
// 参数：owner 为所属窗口；title 为标题；text 为警告内容。返回：无。
void Warn(HWND owner, const wchar_t* title, const wchar_t* text) {
    MessageBoxW(owner, text, title, MB_OK | MB_ICONWARNING);
}

// 作用：通过 Windows 原生文件夹选择器取得截图输出目录。
// 参数：owner 为对话框所属窗口；directory 接收选中的路径；canceled 接收用户是否主动取消。
// 返回：选择了有效文件夹时为 true；取消或对话框初始化失败时为 false。
bool ChooseScreenshotDirectory(HWND owner, std::filesystem::path& directory, bool& canceled) {
    directory.clear();
    canceled = false;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) return false;

    const bool selected = [&] {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result)) return false;

        DWORD options = 0;
        result = dialog->GetOptions(&options);
        if (FAILED(result) || FAILED(dialog->SetOptions(
                options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST))) {
            return false;
        }
        dialog->SetTitle(L"选择截图保存文件夹");
        dialog->SetOkButtonLabel(L"选择文件夹");

        // 核心逻辑：取消是正常用户操作，单独返回状态，避免弹出“保存失败”提示。
        result = dialog->Show(owner);
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            canceled = true;
            return false;
        }
        if (FAILED(result)) return false;

        Microsoft::WRL::ComPtr<IShellItem> folder;
        if (FAILED(dialog->GetResult(folder.GetAddressOf()))) return false;
        PWSTR selected_path = nullptr;
        result = folder->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
        if (FAILED(result) || !selected_path) {
            if (selected_path) CoTaskMemFree(selected_path);
            return false;
        }
        directory = selected_path;
        CoTaskMemFree(selected_path);
        return true;
    }();

    CoUninitialize();
    return selected;
}

// 作用：把大模型分析文本复制到 Windows Unicode 剪贴板。
// 参数：owner 为剪贴板所属窗口；text 为完整分析文本。返回：系统接管文本内存时为 true。
bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!storage) return false;
    auto* destination = static_cast<wchar_t*>(GlobalLock(storage));
    if (!destination) {
        GlobalFree(storage);
        return false;
    }
    std::copy(text.c_str(), text.c_str() + text.size() + 1, destination);
    GlobalUnlock(storage);
    if (!OpenClipboard(owner)) {
        GlobalFree(storage);
        return false;
    }
    const bool published = EmptyClipboard() &&
        SetClipboardData(CF_UNICODETEXT, storage) != nullptr;
    CloseClipboard();
    if (!published) GlobalFree(storage);
    return published;
}

}  // 匿名命名空间结束

App::App() : panel_(catalog_) {}

App::~App() {
    if (hotkey_registered_) UnregisterHotKey(orb_.handle(), kHotkeyId);
}

bool App::Initialize() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    settings_ = settings_store_.Load();
    screenshot_ai_config_ = {settings_.screenshot_ai_api_key,
        settings_.screenshot_ai_base_url, settings_.screenshot_ai_model};
    const auto primary = platform::MonitorAt({0, 0});
    const POINT initial = settings_.orb_position.value_or(POINT{
        primary.work.right - 58, primary.work.top + (primary.work.bottom - primary.work.top) / 3});
    if (!orb_.Create(initial) || !panel_.Create() || !fence_panel_.Create() ||
        !screenshot_window_.Create()) return false;
    orb_.SnapToEdge();
    if (!menu_.Add(L"窗口切换", L"window_switcher.png", [this] { OpenSwitcher(); })) return false;
    if (!menu_.Add(L"截图", L"screenshot.png", [this] { CaptureScreenshot(); })) return false;
    if (!menu_.Add(L"桌面围栏", L"desktop_fence.png", [this] { OpenDesktopFence(); })) return false;
    auto excluded = menu_.Handles();
    excluded.push_back(orb_.handle());
    excluded.push_back(panel_.handle());
    for (HWND handle : fence_panel_.Handles()) excluded.push_back(handle);
    panel_.SetExcluded(std::move(excluded));
    screenshot_window_.on_action = [this](ui::ScreenshotAction action, RECT selection) {
        HandleScreenshotAction(action, selection);
    };
    panel_.on_closed = [this] {
        ShowWindow(orb_.handle(), SW_SHOWNOACTIVATE);
    };
    fence_panel_.on_closed = [this] {
        ShowWindow(orb_.handle(), SW_SHOWNOACTIVATE);
    };
    orb_.on_click = [this] { ToggleMenu(); };
    orb_.on_drag_start = [this] {
        if (fence_panel_.visible()) menu_.Hide();
        else CloseOverlays();
    };
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
    orb_.SetAlpha(Contains(hover_bounds, cursor) || menu_.visible() || panel_.visible() ||
                  fence_panel_.visible() ? 255 : 125);

    const bool left_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (left_down && !previous_left_down_ &&
        (menu_.visible() || panel_.visible())) {
        bool inside = Contains(orb_bounds, cursor);
        if (panel_.visible()) inside = inside || Contains(Bounds(panel_.handle()), cursor);
        if (menu_.visible()) {
            for (HWND button : menu_.Handles()) inside = inside || Contains(Bounds(button), cursor);
        }
        if (!inside) {
            if (fence_panel_.visible()) menu_.Hide();
            else CloseOverlays();
        }
    }
    previous_left_down_ = left_down;

    const bool escape_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escape_down && !previous_escape_down_) {
        if (fence_panel_.visible()) menu_.Hide();
        else CloseOverlays();
    }
    previous_escape_down_ = escape_down;
    PollScreenshotAnalysis();
}

void App::ToggleMenu() {
    if (screenshot_window_.visible()) return;
    if (menu_.visible()) { menu_.Hide(); return; }
    panel_.Hide();
    const RECT bounds = orb_.Bounds();
    const POINT center{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
    menu_.ShowAround(bounds, platform::MonitorAt(center), GetDpiForWindow(orb_.handle()));
}

void App::OpenSwitcher() {
    menu_.Hide();
    fence_panel_.Hide();
    const RECT bounds = orb_.Bounds();
    const POINT center{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
    const auto monitor = platform::MonitorAt(center);
    const auto edge = center.x < (monitor.work.left + monitor.work.right) / 2
        ? ui::SwitcherPanel::Edge::left : ui::SwitcherPanel::Edge::right;
    panel_.ShowFor(monitor, edge);
    if (panel_.visible()) ShowWindow(orb_.handle(), SW_HIDE);
}

void App::OpenDesktopFence() {
    menu_.Hide();
    panel_.Hide();
    if (fence_panel_.visible()) {
        fence_panel_.Hide();
        return;
    }
    const RECT bounds = orb_.Bounds();
    const POINT center{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
    const auto monitor = platform::MonitorAt(center);
    const bool align_right = center.x >= (monitor.work.left + monitor.work.right) / 2;
    fence_panel_.ShowFor(monitor, align_right);
    if (fence_panel_.visible()) {
        // 核心逻辑：围栏卡片失焦后仍保留；悬浮球保持可见并位于卡片上方，作为显式关闭入口。
        SetWindowPos(orb_.handle(), HWND_TOPMOST, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void App::CaptureScreenshot() {
    menu_.Hide();
    panel_.Hide();
    // 核心逻辑：围栏是持续显示的桌面功能，截图不改变其开关状态；
    // 用户若在桌面上截取围栏，快照也应保留当前可见内容。
    ShowWindow(orb_.handle(), SW_HIDE);
    // 核心逻辑：等待 DWM 完成悬浮球隐藏，避免将自身界面残影写入截图。
    Sleep(100);
    std::wstring error;
    if (!screenshot_service_.CaptureAllDisplays(error)) {
        ShowWindow(orb_.handle(), SW_SHOWNOACTIVATE);
        Warn(orb_.handle(), L"截图失败", error.c_str());
        return;
    }
    if (!screenshot_window_.ShowFor(screenshot_service_)) {
        screenshot_service_.Reset();
        ShowWindow(orb_.handle(), SW_SHOWNOACTIVATE);
        Warn(orb_.handle(), L"截图失败", L"无法打开截图选区界面。");
    }
}

void App::HandleScreenshotAction(ui::ScreenshotAction action, RECT selection) {
    switch (action) {
        case ui::ScreenshotAction::copy_analysis_result:
            if (!CopyTextToClipboard(screenshot_window_.handle(), screenshot_window_.analysis_result()))
                Warn(screenshot_window_.handle(), L"复制失败", L"无法将分析结果写入 Windows 剪贴板。");
            return;
        case ui::ScreenshotAction::extract_text:
            StartScreenshotAnalysis(selection, platform::ScreenshotAiTask::extract_text);
            return;
        case ui::ScreenshotAction::summarize:
            StartScreenshotAnalysis(selection, platform::ScreenshotAiTask::summarize);
            return;
        case ui::ScreenshotAction::recapture:
            screenshot_window_.Hide();
            CaptureScreenshot();
            return;
        case ui::ScreenshotAction::close:
            screenshot_window_.Hide();
            screenshot_service_.Reset();
            ShowWindow(orb_.handle(), SW_SHOWNOACTIVATE);
            return;
        case ui::ScreenshotAction::copy: {
            const auto result = screenshot_service_.CopySelection(selection);
            if (!result.copied_to_clipboard) {
                Warn(screenshot_window_.handle(), L"复制失败", result.error.c_str());
                return;
            }
            screenshot_window_.Hide();
            screenshot_service_.Reset();
            ShowWindow(orb_.handle(), SW_SHOWNOACTIVATE);
            return;
        }
        case ui::ScreenshotAction::save: {
            std::filesystem::path directory;
            bool canceled = false;
            if (!ChooseScreenshotDirectory(screenshot_window_.handle(), directory, canceled)) {
                if (!canceled)
                    Warn(screenshot_window_.handle(), L"选择文件夹失败", L"无法打开文件夹选择窗口。");
                return;
            }
            const auto result = screenshot_service_.SaveSelection(selection, directory);
            if (!result.saved) {
                Warn(screenshot_window_.handle(), L"保存失败", result.error.c_str());
                return;
            }
            screenshot_window_.Hide();
            screenshot_service_.Reset();
            ShowWindow(orb_.handle(), SW_SHOWNOACTIVATE);
            return;
        }
    }
}

void App::StartScreenshotAnalysis(RECT selection, platform::ScreenshotAiTask task) {
    if (screenshot_ai_pending_) return;

    std::vector<std::uint8_t> image_png;
    std::wstring error;
    if (!screenshot_service_.EncodeSelectionPng(selection, image_png, error)) {
        Warn(screenshot_window_.handle(), L"截图分析失败", error.c_str());
        return;
    }

    screenshot_ai_task_ = task;
    screenshot_ai_pending_ = true;
    screenshot_window_.SetAnalysisPending(true);
    try {
        // 核心逻辑：网络请求在后台执行；配置按值复制，避免后台线程读取 UI 线程正在更新的设置。
        const platform::ScreenshotAiConfig config = screenshot_ai_config_;
        screenshot_ai_future_ = std::async(std::launch::async,
            [image = std::move(image_png), task, config] {
                return platform::AnalyzeScreenshot(image, task, config);
            });
    } catch (const std::exception&) {
        screenshot_ai_pending_ = false;
        screenshot_window_.SetAnalysisPending(false);
        Warn(screenshot_window_.handle(), L"截图分析失败", L"无法启动后台分析任务。");
    }
}

void App::PollScreenshotAnalysis() {
    if (!screenshot_ai_pending_ || !screenshot_ai_future_.valid() ||
        screenshot_ai_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    platform::ScreenshotAiResult result;
    try {
        result = screenshot_ai_future_.get();
    } catch (const std::exception&) {
        result.error = L"后台分析任务意外终止。";
    }
    screenshot_ai_pending_ = false;
    screenshot_window_.SetAnalysisPending(false);

    const HWND owner = screenshot_window_.visible() ? screenshot_window_.handle() : orb_.handle();
    if (!result.succeeded) {
        if (screenshot_window_.visible()) {
            screenshot_window_.SetAnalysisResult(L"分析失败", std::move(result.error));
        } else {
            Warn(owner, L"截图分析失败", result.error.c_str());
        }
        return;
    }
    const wchar_t* title = screenshot_ai_task_ == platform::ScreenshotAiTask::extract_text
        ? L"截图文字提取结果" : L"截图内容总结";
    screenshot_window_.SetAnalysisResult(title, std::move(result.text));
}

void App::CloseOverlays() {
    menu_.Hide();
    panel_.Hide();
    fence_panel_.Hide();
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
        // 核心逻辑：整体更新已加载设置，避免后续保存悬浮球位置时覆盖刚编辑的 AI 参数。
        settings_ = next;
        screenshot_ai_config_ = {settings_.screenshot_ai_api_key,
            settings_.screenshot_ai_base_url, settings_.screenshot_ai_model};
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

}  // desktop_orb 命名空间结束
