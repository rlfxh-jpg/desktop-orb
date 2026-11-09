// 文件职责：以 GDI 绘制截图标注，并通过缩小再放大的像素块实现不可逆马赛克。
// 本模块仅借用目标 HDC，所有临时 GDI 对象在单次调用内释放。
#include "desktop_orb/platform/screenshot/screenshot_edits.hpp"

#include <algorithm>

namespace desktop_orb::platform {

// 作用：在目标图像上先处理马赛克，再绘制笔迹。
// 参数：dc 为包含原始选区的目标 HDC；width、height 为图像尺寸；edits 为编辑数据。
// 返回：所有编辑成功时为 true；无效尺寸或任一效果失败时为 false。
bool ApplyScreenshotEdits(HDC dc, int width, int height, const ScreenshotEdits& edits) {
    if (!dc || width <= 0 || height <= 0) return false;
    const int saved = SaveDC(dc);
    if (!saved) return false;
    bool succeeded = true;
    IntersectClipRect(dc, 0, 0, width, height);
    for (RECT area : edits.pixelated_areas) {
        area.left = std::clamp<LONG>(area.left, 0, width);
        area.right = std::clamp<LONG>(area.right, 0, width);
        area.top = std::clamp<LONG>(area.top, 0, height);
        area.bottom = std::clamp<LONG>(area.bottom, 0, height);
        const int w = area.right - area.left;
        const int h = area.bottom - area.top;
        if (w < 2 || h < 2) continue;
        const int block_w = std::max(1, w / 12);
        const int block_h = std::max(1, h / 12);
        HDC small = CreateCompatibleDC(dc);
        HBITMAP bitmap = small ? CreateCompatibleBitmap(dc, block_w, block_h) : nullptr;
        if (!bitmap) { if (small) DeleteDC(small); succeeded = false; continue; }
        HGDIOBJ previous = SelectObject(small, bitmap);
        if (!previous || previous == HGDI_ERROR) {
            DeleteObject(bitmap);
            DeleteDC(small);
            succeeded = false;
            continue;
        }
        // 核心逻辑：先把原像素压缩到低分辨率表面，再用 COLORONCOLOR 放大，输出不保留原细节。
        SetStretchBltMode(small, COLORONCOLOR);
        if (StretchBlt(small, 0, 0, block_w, block_h, dc,
                area.left, area.top, w, h, SRCCOPY)) {
            SetStretchBltMode(dc, COLORONCOLOR);
            if (!StretchBlt(dc, area.left, area.top, w, h, small,
                0, 0, block_w, block_h, SRCCOPY)) succeeded = false;
        } else succeeded = false;
        SelectObject(small, previous);
        DeleteObject(bitmap);
        DeleteDC(small);
    }
    HPEN pen = CreatePen(PS_SOLID, 4, RGB(243, 58, 67));
    if (pen) {
        HGDIOBJ previous = SelectObject(dc, pen);
        if (!previous || previous == HGDI_ERROR) succeeded = false;
        else {
        for (const auto& stroke : edits.strokes) {
            if (stroke.points.empty()) continue;
            MoveToEx(dc, stroke.points.front().x, stroke.points.front().y, nullptr);
            if (stroke.points.size() == 1) {
                LineTo(dc, stroke.points.front().x + 1, stroke.points.front().y + 1);
            } else {
                for (size_t index = 1; index < stroke.points.size(); ++index)
                    LineTo(dc, stroke.points[index].x, stroke.points[index].y);
            }
        }
        SelectObject(dc, previous);
        }
        DeleteObject(pen);
    } else if (!edits.strokes.empty()) succeeded = false;
    RestoreDC(dc, saved);
    return succeeded;
}

}  // desktop_orb::platform 命名空间结束
