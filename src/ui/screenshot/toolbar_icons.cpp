// 文件职责：用 GDI 基本图形绘制截图工具栏的十四个内嵌图标。
// 图标坐标统一为 32×32，再映射到实际按钮尺寸；GDI 画笔在单次绘制后释放。
#include "desktop_orb/ui/screenshot/toolbar_icons.hpp"
#include "desktop_orb/ui/screenshot/selection_window.hpp"

#include <algorithm>
#include <initializer_list>

namespace desktop_orb::ui {
namespace {

// 作用：把标准 32×32 图标坐标映射到目标图标区域。
// 参数：bounds 为实际绘制矩形；x、y 为标准坐标。返回：目标设备坐标。
POINT MapPoint(RECT bounds, int x, int y) {
    return {bounds.left + (bounds.right - bounds.left) * x / 32,
        bounds.top + (bounds.bottom - bounds.top) * y / 32};
}

// 作用：绘制一条线段。
// 参数：dc 为已选入画笔的设备上下文；bounds 为图标范围；起点和终点为标准坐标。
// 返回：无；GDI 失败时由调用方的整体绘制结果处理。
void Line(HDC dc, RECT bounds, int x1, int y1, int x2, int y2) {
    const POINT start = MapPoint(bounds, x1, y1);
    const POINT end = MapPoint(bounds, x2, y2);
    MoveToEx(dc, start.x, start.y, nullptr);
    LineTo(dc, end.x, end.y);
}

// 作用：连接一组标准坐标点，形成图标轮廓。
// 参数：dc 为设备上下文；bounds 为图标范围；points 为依次连接的坐标。
// 返回：无；少于两个点时不绘制。
void Path(HDC dc, RECT bounds, std::initializer_list<POINT> points) {
    if (points.size() < 2) return;
    auto point = points.begin();
    POINT previous = *point++;
    for (; point != points.end(); ++point) {
        Line(dc, bounds, previous.x, previous.y, point->x, point->y);
        previous = *point;
    }
}

// 作用：绘制矩形线框。
// 参数：dc 为设备上下文；bounds 为图标范围；left、top、right、bottom 为标准坐标。
// 返回：无。
void Box(HDC dc, RECT bounds, int left, int top, int right, int bottom) {
    const POINT start = MapPoint(bounds, left, top);
    const POINT end = MapPoint(bounds, right, bottom);
    Rectangle(dc, start.x, start.y, end.x, end.y);
}

// 作用：绘制圆形线框。
// 参数：dc 为设备上下文；bounds 为图标范围；left、top、right、bottom 为标准坐标。
// 返回：无。
void Circle(HDC dc, RECT bounds, int left, int top, int right, int bottom) {
    const POINT start = MapPoint(bounds, left, top);
    const POINT end = MapPoint(bounds, right, bottom);
    Ellipse(dc, start.x, start.y, end.x, end.y);
}

}  // 匿名命名空间结束

// 作用：按功能绘制白色或指定颜色的线框图标。
// 参数：dc 为目标 HDC；action 为操作；bounds 为图标范围；color 为描边颜色。
// 返回：有效功能完成绘制为 true；绘制环境不可用或功能无图标时为 false。
bool DrawScreenshotToolbarIcon(HDC dc, ScreenshotAction action, RECT bounds, COLORREF color) {
    if (!dc || bounds.right <= bounds.left || bounds.bottom <= bounds.top ||
        action == ScreenshotAction::none || action == ScreenshotAction::copy_analysis_result) return false;
    const int stroke = std::max(2, static_cast<int>(bounds.right - bounds.left) / 12);
    HPEN pen = CreatePen(PS_SOLID, stroke, color);
    if (!pen) return false;
    const int saved = SaveDC(dc);
    if (!saved) { DeleteObject(pen); return false; }
    SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    // 核心逻辑：图标只使用矢量线段，界面缩放时无需维护多套位图资源。
    bool known_action = true;
    switch (action) {
        case ScreenshotAction::extract_text:
            Path(dc, bounds, {{9, 6}, {5, 6}, {5, 11}});
            Path(dc, bounds, {{23, 6}, {27, 6}, {27, 11}});
            Path(dc, bounds, {{5, 21}, {5, 26}, {9, 26}});
            Path(dc, bounds, {{27, 21}, {27, 26}, {23, 26}});
            Line(dc, bounds, 10, 12, 22, 12);
            Line(dc, bounds, 10, 17, 22, 17);
            Line(dc, bounds, 10, 22, 18, 22);
            break;
        case ScreenshotAction::summarize:
            Box(dc, bounds, 7, 4, 23, 28);
            Line(dc, bounds, 11, 10, 19, 10);
            Line(dc, bounds, 11, 15, 19, 15);
            Line(dc, bounds, 11, 20, 17, 20);
            Line(dc, bounds, 25, 5, 25, 11);
            Line(dc, bounds, 22, 8, 28, 8);
            break;
        case ScreenshotAction::translate:
            Path(dc, bounds, {{5, 22}, {11, 8}, {17, 22}});
            Line(dc, bounds, 8, 17, 14, 17);
            Path(dc, bounds, {{18, 12}, {27, 12}, {27, 17}});
            Path(dc, bounds, {{27, 19}, {23, 24}, {19, 19}});
            break;
        case ScreenshotAction::copy:
            Box(dc, bounds, 10, 10, 26, 27);
            Path(dc, bounds, {{21, 7}, {6, 7}, {6, 23}});
            break;
        case ScreenshotAction::save:
            Line(dc, bounds, 16, 5, 16, 21);
            Path(dc, bounds, {{10, 15}, {16, 21}, {22, 15}});
            Path(dc, bounds, {{6, 22}, {6, 27}, {26, 27}, {26, 22}});
            break;
        case ScreenshotAction::recapture:
            Path(dc, bounds, {{24, 11}, {21, 7}, {16, 5}, {10, 7}, {6, 12}, {5, 18},
                {8, 24}, {14, 27}, {21, 25}, {26, 20}});
            Path(dc, bounds, {{19, 11}, {24, 11}, {25, 6}});
            break;
        case ScreenshotAction::pin_image:
            Path(dc, bounds, {{11, 5}, {21, 5}, {20, 11}, {24, 16}, {24, 18}, {8, 18},
                {8, 16}, {12, 11}, {11, 5}});
            Line(dc, bounds, 16, 18, 16, 28);
            break;
        case ScreenshotAction::pen:
            Path(dc, bounds, {{7, 24}, {10, 17}, {23, 5}, {27, 9}, {14, 22}, {7, 24}});
            Line(dc, bounds, 10, 17, 14, 22);
            Line(dc, bounds, 5, 28, 27, 28);
            break;
        case ScreenshotAction::add_text:
            Line(dc, bounds, 6, 7, 26, 7);
            Line(dc, bounds, 16, 7, 16, 26);
            Line(dc, bounds, 11, 26, 21, 26);
            break;
        case ScreenshotAction::pixelate:
            Box(dc, bounds, 5, 5, 13, 13);
            Box(dc, bounds, 16, 5, 24, 13);
            Box(dc, bounds, 5, 16, 13, 24);
            Box(dc, bounds, 16, 16, 24, 24);
            break;
        case ScreenshotAction::eraser:
            Path(dc, bounds, {{5, 21}, {17, 7}, {27, 16}, {16, 27}, {11, 27}, {5, 21}});
            Line(dc, bounds, 11, 15, 22, 22);
            break;
        case ScreenshotAction::undo:
            Path(dc, bounds, {{12, 9}, {5, 16}, {12, 23}});
            Path(dc, bounds, {{6, 16}, {18, 16}, {23, 19}, {25, 24}});
            break;
        case ScreenshotAction::redo:
            Path(dc, bounds, {{20, 9}, {27, 16}, {20, 23}});
            Path(dc, bounds, {{26, 16}, {14, 16}, {9, 19}, {7, 24}});
            break;
        case ScreenshotAction::close:
            Line(dc, bounds, 7, 7, 25, 25);
            Line(dc, bounds, 25, 7, 7, 25);
            break;
        default: known_action = false; break;
    }
    RestoreDC(dc, saved);
    DeleteObject(pen);
    return known_action;
}

}  // desktop_orb::ui 命名空间结束
