// 文件职责：查询显示器边界和工作区，并提供界面 DPI 缩放计算。
// 本模块不持有 HMONITOR，仅把系统提供的句柄与矩形打包返回。
#include "desktop_orb/platform/monitor.hpp"

#include <algorithm>

namespace desktop_orb::platform {
namespace {

// 作用：从显示器句柄读取边界和工作区信息。
// 参数：handle 为目标 HMONITOR。返回：Monitor；系统查询失败时矩形可能为空。
Monitor FromHandle(HMONITOR handle) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(handle, &info);
    Monitor monitor{handle, info.rcMonitor, info.rcWork};
    // 核心约束：窗口放到目标显示器后使用 GetDpiForWindow 获取实际 DPI。
    monitor.dpi = 96;
    return monitor;
}

// 作用：接收 EnumDisplayMonitors 的每个显示器并追加到列表。
// 参数：handle 为显示器句柄；未命名 HDC 和矩形参数不使用；data 指向列表。
// 返回：TRUE 表示继续枚举。
BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM data) {
    auto* monitors = reinterpret_cast<std::vector<Monitor>*>(data);
    monitors->push_back(FromHandle(handle));
    return TRUE;
}

}  // 匿名命名空间结束

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

}  // desktop_orb::platform 命名空间结束
