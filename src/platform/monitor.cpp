#include "desktop_orb/platform/monitor.hpp"

#include <algorithm>

namespace desktop_orb::platform {
namespace {

Monitor FromHandle(HMONITOR handle) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(handle, &info);
    Monitor monitor{handle, info.rcMonitor, info.rcWork};
    // GetDpiForWindow reflects per-monitor DPI once a window is on this monitor.
    monitor.dpi = 96;
    return monitor;
}

BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM data) {
    auto* monitors = reinterpret_cast<std::vector<Monitor>*>(data);
    monitors->push_back(FromHandle(handle));
    return TRUE;
}

}  // namespace

Monitor MonitorAt(POINT point) {
    return FromHandle(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST));
}

Monitor MonitorOf(HWND window) {
    return FromHandle(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
}

std::vector<Monitor> AllMonitors() {
    std::vector<Monitor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
    std::sort(monitors.begin(), monitors.end(), [](const Monitor& a, const Monitor& b) {
        if (a.bounds.left != b.bounds.left) return a.bounds.left < b.bounds.left;
        return a.bounds.top < b.bounds.top;
    });
    return monitors;
}

int Scale(int pixels, UINT dpi) {
    return MulDiv(pixels, static_cast<int>(dpi ? dpi : 96), 96);
}

}  // namespace desktop_orb::platform
