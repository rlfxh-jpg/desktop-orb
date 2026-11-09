#pragma once

// 文件职责：定义截图标注笔迹与马赛克区域，并提供共同的 GDI 像素处理入口。
// 坐标均相对截图选区左上角；编辑数据不持有窗口、位图或设备上下文。
#include <vector>
#include <windows.h>

namespace desktop_orb::platform {

struct ScreenshotStroke {
    std::vector<POINT> points;
};

struct ScreenshotEdits {
    std::vector<ScreenshotStroke> strokes;
    std::vector<RECT> pixelated_areas;
};

// 作用：把马赛克和红色笔迹绘制到已有截图像素表面。
// 参数：dc 为目标设备上下文且包含原图；width、height 为选区尺寸；edits 为相对坐标编辑数据。
// 返回：全部效果绘制成功为 true；资源或绘图失败时为 false，不修改编辑数据。
bool ApplyScreenshotEdits(HDC dc, int width, int height, const ScreenshotEdits& edits);

}  // desktop_orb::platform 命名空间结束
