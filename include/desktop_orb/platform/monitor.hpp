#pragma once

// 文件职责：提供显示器信息、跨屏枚举和 DPI 缩放计算。
// 只封装 Win32 显示器查询，不决定任何界面布局策略。
#include <windows.h>
#include <vector>

namespace desktop_orb::platform {

struct Monitor {
    HMONITOR handle = nullptr;
    RECT bounds{};
    RECT work{};
    UINT dpi = 96;
};

// 作用：查找包含屏幕坐标的显示器，找不到时取最近显示器。
// 参数：point 为屏幕坐标。返回：该显示器的边界、工作区和句柄。
[[nodiscard]] Monitor MonitorAt(POINT point);
// 作用：查找窗口所在或最近的显示器。
// 参数：window 为目标窗口句柄。返回：对应显示器信息。
[[nodiscard]] Monitor MonitorOf(HWND window);
// 作用：枚举并按屏幕位置排序所有显示器。参数：无。
// 返回：显示器列表；枚举失败时可能为空。
[[nodiscard]] std::vector<Monitor> AllMonitors();
// 作用：把 96 DPI 下的逻辑像素转换为目标 DPI 像素。
// 参数：pixels 为逻辑像素；dpi 为目标 DPI，0 按 96 处理。返回：缩放后像素。
[[nodiscard]] int Scale(int pixels, UINT dpi);

}  // desktop_orb::platform 命名空间结束
