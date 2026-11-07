#include "desktop_orb/platform/window_catalog.hpp"

#include "desktop_orb/platform/monitor.hpp"
#include <algorithm>
#include <dwmapi.h>

namespace desktop_orb::platform {
namespace {

BOOL CALLBACK CollectWindow(HWND window, LPARAM data) {
    auto* handles = reinterpret_cast<std::vector<HWND>*>(data);
    handles->push_back(window);
    return TRUE;
}

std::wstring Title(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    title.resize(static_cast<size_t>(std::max(copied, 0)));
    return title;
}

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

}  // namespace

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

}  // namespace desktop_orb::platform
