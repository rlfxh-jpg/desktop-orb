// 文件职责：以 GDI 绘制截图笔迹与文字，并通过缩小再放大的像素块实现马赛克。
// 本模块仅借用目标 HDC，所有临时 GDI 对象在单次调用内释放。
#include "desktop_orb/platform/screenshot/screenshot_edits.hpp"

#include <algorithm>

namespace desktop_orb::platform {
namespace {

// 作用：创建与截图物理像素字号一致的中文字体。
// 参数：height 为文字对象记录的像素高度。返回：新建字体句柄；创建失败时为空。
HFONT CreateTextFont(int height) {
    return CreateFontW(-std::max(1, height), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
}

}  // 匿名命名空间结束

// 作用：按实际字体计算文字在选区内占用的可见范围。
// 参数：dc 为测量用设备上下文；width、height 为选区尺寸；text 为文字对象。
// 返回：相对选区矩形；文字为空、位置越界或 GDI 失败时为空矩形。
RECT ScreenshotTextBounds(HDC dc, int width, int height, const ScreenshotText& text) {
    if (!dc || text.content.empty() || width <= 0 || height <= 0 ||
        text.origin.x >= width || text.origin.y >= height) return {};
    HFONT font = CreateTextFont(text.font_height);
    if (!font) return {};
    HGDIOBJ previous = SelectObject(dc, font);
    if (!previous || previous == HGDI_ERROR) { DeleteObject(font); return {}; }
    // 核心逻辑：负坐标允许选区左上边缘向内拉伸后保留原屏幕位置；最终只返回可见部分。
    RECT measured{std::max<LONG>(0, text.origin.x), std::max<LONG>(0, text.origin.y),
        width, height};
    RECT layout{text.origin.x, text.origin.y, width, height};
    DrawTextW(dc, text.content.c_str(), -1, &layout,
        DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    measured.right = std::min<LONG>(width, layout.right);
    measured.bottom = std::min<LONG>(height, layout.bottom);
    SelectObject(dc, previous);
    DeleteObject(font);
    if (measured.right <= measured.left || measured.bottom <= measured.top) return {};
    return measured;
}

// 作用：在目标图像上依次处理马赛克、笔迹和文字。
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
    for (const auto& annotation : edits.texts) {
        if (annotation.content.empty()) continue;
        const RECT bounds = ScreenshotTextBounds(dc, width, height, annotation);
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) continue;
        HFONT font = CreateTextFont(annotation.font_height);
        if (!font) { succeeded = false; continue; }
        HGDIOBJ previous = SelectObject(dc, font);
        if (!previous || previous == HGDI_ERROR) {
            DeleteObject(font);
            succeeded = false;
            continue;
        }
        SetBkMode(dc, TRANSPARENT);
        const UINT format = DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL;
        // 核心逻辑：同一文字先画深色阴影再画白色正文，明暗截图上都可辨认。
        RECT shadow{annotation.origin.x + 1, annotation.origin.y + 1, width, height};
        SetTextColor(dc, RGB(20, 28, 32));
        if (!DrawTextW(dc, annotation.content.c_str(), -1, &shadow, format)) succeeded = false;
        RECT body{annotation.origin.x, annotation.origin.y, width, height};
        SetTextColor(dc, RGB(255, 255, 255));
        if (!DrawTextW(dc, annotation.content.c_str(), -1, &body, format)) succeeded = false;
        SelectObject(dc, previous);
        DeleteObject(font);
    }
    RestoreDC(dc, saved);
    return succeeded;
}

}  // desktop_orb::platform 命名空间结束
