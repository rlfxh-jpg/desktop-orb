#pragma once

// 文件职责：定义环绕菜单的几何布局结果；只计算屏幕坐标，不创建或持有 Win32 窗口。
// 输入坐标和尺寸均为物理像素，DPI 缩放由布局函数统一处理。
#include <cstddef>
#include <vector>
#include <windows.h>

namespace desktop_orb::ui {

inline constexpr int kDefaultFeatureDiameter = 44;
inline constexpr int kHoveredFeatureDiameter = 52;

struct FeatureLayout {
    int default_diameter = 0;
    int hovered_diameter = 0;
    std::vector<POINT> centers;
};

// 作用：按悬浮球位置、显示器工作区和入口数量计算功能按钮中心。
// 参数：orb 为悬浮球屏幕矩形；work 为所在显示器工作区；dpi 为显示器缩放值；
//       count 为功能入口数量。
// 返回：按钮直径及各入口中心；工作区无效或入口数为零时 centers 为空。
[[nodiscard]] FeatureLayout ArrangeFeatures(RECT orb, RECT work, UINT dpi, size_t count);

}  // desktop_orb::ui 命名空间结束
