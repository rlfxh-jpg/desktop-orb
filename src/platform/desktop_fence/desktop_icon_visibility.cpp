// 文件职责：查找 Explorer 桌面图标视图，在围栏生命周期内临时隐藏并可靠恢复。
// HWND 属于 Explorer；本模块只借用句柄，保护进程在主程序异常退出时负责兜底恢复。
#include "desktop_orb/platform/desktop_fence/desktop_icon_visibility.hpp"
#include <cerrno>
#include <cstdlib>
#include <cwchar>
#include <shellapi.h>
#include <string>

namespace desktop_orb::platform {
namespace {

// 作用：检查桌面宿主的子窗口是否为图标列表，并收集其句柄。
// 参数：window 为子窗口；context 为接收句柄的 vector 指针。返回：TRUE，继续枚举。
BOOL CALLBACK CollectIconChild(HWND window, LPARAM context) {
    wchar_t class_name[32]{};
    if (!GetClassNameW(window, class_name, 32) ||
        std::wcscmp(class_name, L"SysListView32") != 0) return TRUE;
    HWND parent = GetParent(window);
    if (!parent || !GetClassNameW(parent, class_name, 32) ||
        std::wcscmp(class_name, L"SHELLDLL_DefView") != 0) return TRUE;
    auto* views = reinterpret_cast<std::vector<HWND>*>(context);
    views->push_back(window);
    return TRUE;
}

// 作用：从 Explorer 的 Progman 或 WorkerW 顶层宿主中收集桌面图标列表。
// 参数：window 为顶层窗口；context 为接收句柄的 vector 指针。返回：TRUE，继续枚举。
BOOL CALLBACK CollectDesktopViews(HWND window, LPARAM context) {
    wchar_t class_name[32]{};
    if (!GetClassNameW(window, class_name, 32) ||
        (std::wcscmp(class_name, L"Progman") != 0 &&
         std::wcscmp(class_name, L"WorkerW") != 0)) return TRUE;
    // 核心逻辑：Explorer 可能在桌面宿主中调整视图层级；遍历后代窗口比固定查找
    // 一层 SHELLDLL_DefView 再查找一层 SysListView32 更可靠。
    EnumChildWindows(window, CollectIconChild, context);
    return TRUE;
}

// 作用：验证句柄仍是 Explorer 桌面视图下的图标列表，避免窗口句柄复用时误操作。
// 参数：window 为之前记录的图标列表 HWND。返回：窗口及其父窗口类型均符合时为 true。
bool IsDesktopIconList(HWND window) {
    if (!IsWindow(window)) return false;
    wchar_t class_name[32]{};
    if (!GetClassNameW(window, class_name, 32) ||
        std::wcscmp(class_name, L"SysListView32") != 0) return false;
    HWND parent = GetParent(window);
    return parent && GetClassNameW(parent, class_name, 32) &&
        std::wcscmp(class_name, L"SHELLDLL_DefView") == 0;
}

// 作用：启动仅等待本次围栏会话结束的辅助进程。
// 参数：views 为隐藏前可见的 Explorer 图标列表；guard_event 接收可通知正常恢复的事件。
// 返回：进程与事件均创建成功为 true；失败时事件保持为空且不应隐藏桌面图标。
bool StartGuard(const std::vector<HWND>& views, HANDLE& guard_event) {
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE event = CreateEventW(&attributes, TRUE, FALSE, nullptr);
    if (!event) return false;

    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) {
        CloseHandle(event);
        return false;
    }
    path.resize(length);
    // 核心逻辑：事件句柄通过可继承句柄传给子进程；子进程持有主进程句柄，
    // 即使主进程被强制结束，也能在进程对象发出信号后恢复原本可见的图标列表。
    std::wstring command = L"\"" + path + L"\" --desktop-icon-guard " +
        std::to_wstring(GetCurrentProcessId()) + L" " +
        std::to_wstring(reinterpret_cast<uintptr_t>(event));
    for (HWND window : views)
        command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(window));

    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!created) {
        CloseHandle(event);
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    guard_event = event;
    return true;
}

// 作用：把命令行中的十进制句柄或进程 ID 转成无符号整数。
// 参数：text 为待解析字符串；value 接收结果。返回：整个字符串均有效且没有溢出时为 true。
bool ParseUnsigned(const wchar_t* text, unsigned long long& value) {
    if (!text || !*text || *text == L'-') return false;
    wchar_t* end = nullptr;
    errno = 0;
    value = std::wcstoull(text, &end, 10);
    return errno != ERANGE && end && *end == L'\0' && end != text;
}

}  // 匿名命名空间结束

// 作用：确保对象离开作用域时 Explorer 桌面视图不会遗留为隐藏状态。
// 参数：无。返回：无。
DesktopIconVisibility::~DesktopIconVisibility() {
    Restore();
}

// 作用：暂时隐藏当前可见的桌面图标视图并记住其原始状态。
// 参数：无。返回：找到至少一个桌面视图为 true；没有桌面视图或枚举失败时为 false。
bool DesktopIconVisibility::Hide() {
    if (!views_.empty()) return true;

    std::vector<HWND> windows;
    EnumWindows(CollectDesktopViews, reinterpret_cast<LPARAM>(&windows));
    std::vector<HWND> visible_views;
    for (HWND window : windows) {
        if (!IsWindow(window)) continue;
        const bool was_visible = IsWindowVisible(window) != FALSE;
        views_.push_back({window, was_visible});
        if (was_visible) visible_views.push_back(window);
    }
    // 核心逻辑：必须先确认保护进程已创建，再更改 Explorer 的跨进程窗口状态。
    // 如果保护进程无法启动，围栏仍可显示，只保留原有桌面图标作为安全降级。
    if (!visible_views.empty() && !StartGuard(visible_views, guard_event_)) {
        views_.clear();
        return false;
    }
    for (HWND window : visible_views)
        if (IsDesktopIconList(window)) ShowWindow(window, SW_HIDE);
    return !views_.empty();
}

// 作用：恢复每个桌面图标视图隐藏前的可见状态并清除快照。
// 参数：无。返回：无；已失效的 Explorer 窗口会安全跳过。
void DesktopIconVisibility::Restore() {
    for (const ViewState& state : views_) {
        if (!IsDesktopIconList(state.window)) continue;
        ShowWindow(state.window, state.was_visible ? SW_SHOWNA : SW_HIDE);
    }
    views_.clear();
    if (guard_event_) {
        // 核心逻辑：正常恢复完成后通知辅助进程退出；强制终止时此处不会执行，
        // 辅助进程会等待主进程结束并自行恢复原本可见的图标。
        SetEvent(guard_event_);
        CloseHandle(guard_event_);
        guard_event_ = nullptr;
    }
}

// 作用：等待主进程正常恢复或异常退出，并在后一种情况下恢复 Explorer 图标。
// 参数：无；参数通过当前命令行读取。返回：0 表示等待结束，1 表示参数或句柄无效。
int RunDesktopIconGuard() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments || count < 5) {
        if (arguments) LocalFree(arguments);
        return 1;
    }
    unsigned long long parent_id = 0;
    unsigned long long event_value = 0;
    const bool valid = ParseUnsigned(arguments[2], parent_id) &&
        ParseUnsigned(arguments[3], event_value) && parent_id <= MAXDWORD &&
        event_value <= UINTPTR_MAX;
    if (!valid) {
        LocalFree(arguments);
        return 1;
    }
    const HANDLE event = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(event_value));
    const HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parent_id));
    bool restore = !parent;
    if (parent) {
        const HANDLE waits[]{event, parent};
        const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        restore = result == WAIT_OBJECT_0 + 1;
        CloseHandle(parent);
    }
    if (restore) {
        for (int index = 4; index < count; ++index) {
            unsigned long long window_value = 0;
            if (!ParseUnsigned(arguments[index], window_value) || window_value > UINTPTR_MAX)
                continue;
            HWND window = reinterpret_cast<HWND>(static_cast<uintptr_t>(window_value));
            if (IsDesktopIconList(window)) ShowWindow(window, SW_SHOWNA);
        }
    }
    CloseHandle(event);
    LocalFree(arguments);
    return 0;
}

}  // desktop_orb::platform 命名空间结束
