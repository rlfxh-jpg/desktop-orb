#pragma once

// 文件职责：定义截图笔迹、马赛克和文字标注，并提供共同的 GDI 像素处理入口。
// 坐标均相对截图选区左上角；编辑数据不持有窗口、位图或设备上下文。
#include <string>
#include <vector>
#include <windows.h>

namespace desktop_orb::platform {

struct ScreenshotStroke {
    std::vector<POINT> points;
};

// 作用：保存一段相对选区左上角定位的文字，宽度由所在选区的剩余空间决定。
// 参数：无。返回：无；字号是截图物理像素，导出时保持与预览一致。
struct ScreenshotText {
    POINT origin{};
    std::wstring content;
    int font_height = 24;
};

struct ScreenshotEdits {
    std::vector<ScreenshotStroke> strokes;
    std::vector<RECT> pixelated_areas;
    std::vector<ScreenshotText> texts;
};

// 作用：计算文字在选区内的可见矩形，供绘制和橡皮擦共用。
// 参数：dc 为测量用设备上下文；width、height 为选区尺寸；text 为文字对象。
// 返回：文字占用的相对选区矩形；无效参数或字体创建失败时为空矩形。
RECT ScreenshotTextBounds(HDC dc, int width, int height, const ScreenshotText& text);

// 作用：把马赛克、红色笔迹和文字绘制到已有截图像素表面。
// 参数：dc 为目标设备上下文且包含原图；width、height 为选区尺寸；edits 为相对坐标编辑数据。
// 返回：全部效果绘制成功为 true；资源或绘图失败时为 false，不修改编辑数据。
bool ApplyScreenshotEdits(HDC dc, int width, int height, const ScreenshotEdits& edits);

}  // desktop_orb::platform 命名空间结束
