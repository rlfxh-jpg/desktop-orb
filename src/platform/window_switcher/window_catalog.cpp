// 文件职责：枚举和筛选 Windows 应用窗口，维护最近使用顺序并请求切换。
// HWND 仅作临时标识；窗口可能随时失效，激活时再次检查有效性。
#include "desktop_orb/platform/window_switcher/window_catalog.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>
#include <dwmapi.h>

namespace desktop_orb::platform {
namespace {

// 作用：收集 EnumWindows 回调提供的顶层窗口句柄。
// 参数：window 为当前 HWND；data 指向句柄列表。返回：TRUE 表示继续枚举。
BOOL CALLBACK CollectWindow(HWND window, LPARAM data) {
    auto* handles = reinterpret_cast<std::vector<HWND>*>(data);
    handles->push_back(window);
    return TRUE;
}

// 作用：读取窗口标题并裁去未使用的缓冲区。
// 参数：window 为目标 HWND。返回：标题；无标题或读取失败时为空。
std::wstring Title(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    title.resize(static_cast<size_t>(std::max(copied, 0)));
    return title;
}

// 作用：按任务切换规则筛选普通应用窗口。
// 参数：window 为候选 HWND；excluded 为本程序等需要排除的句柄。
// 返回：可切换为 true；隐藏、辅助或系统窗口等情况为 false。
bool IsSwitchable(HWND window, const std::vector<HWND>& excluded) {
    if (!window || window == GetShellWindow() || !IsWindowVisible(window)) return false;
    if (std::find(excluded.begin(), excluded.end(), window) != excluded.end()) return false;
    if (Title(window).empty()) return false;
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (style & WS_EX_TOOLWINDOW) return false;
    if (GetWindow(window, GW_OWNER) && !(style & WS_EX_APPWINDOW)) return false;
    wchar_t class_name[128]{};
    GetClassNameW(window, class_name, 128);
    if (wcscmp(class_name, L"Progman") == 0 || wcscmp(class_name, L"WorkerW") == 0 ||
        wcscmp(class_name, L"Shell_TrayWnd") == 0 || wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0) return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    return true;
}

}  // 匿名命名空间结束

void WindowCatalog::ObserveForeground() {
    const HWND current = GetForegroundWindow();
    if (current && current != last_foreground_) {
        last_foreground_ = current;
        recency_[current] = ++sequence_;
    }
}

std::vector<WindowInfo> WindowCatalog::List(const std::vector<HWND>& excluded) const {
    std::vector<HWND> handles;
    EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&handles));
    const auto monitors = AllMonitors();
    std::vector<WindowInfo> windows;
    for (size_t z = 0; z < handles.size(); ++z) {
        const HWND window = handles[z];
        if (!IsSwitchable(window, excluded)) continue;
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        const auto match = std::find_if(monitors.begin(), monitors.end(),
                                        [monitor](const Monitor& item) { return item.handle == monitor; });
        const int number = match == monitors.end() ? 1 : static_cast<int>(std::distance(monitors.begin(), match)) + 1;
        const auto recent = recency_.find(window);
        windows.push_back({window, Title(window), number, IsIconic(window) != FALSE,
                           recent == recency_.end() ? 0 : recent->second, z});
    }
    std::stable_sort(windows.begin(), windows.end(), [](const WindowInfo& a, const WindowInfo& b) {
        if (a.last_used != b.last_used) return a.last_used > b.last_used;
        return a.z_order < b.z_order;
    });
    return windows;
}

bool WindowCatalog::Activate(HWND window) {
    if (!IsWindow(window)) return false;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    return SetForegroundWindow(window) != FALSE;
}

}  // desktop_orb::platform 命名空间结束
