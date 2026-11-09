// 文件职责：验证截图画笔、马赛克和文字会修改输出位图的真实像素。
// 测试只使用内存中的 32 位 DIB，不读取桌面或剪贴板。
#include "desktop_orb/platform/screenshot/screenshot_edits.hpp"

#include <windows.h>

using desktop_orb::platform::ApplyScreenshotEdits;
using desktop_orb::platform::ScreenshotEdits;

// 作用：构造带有棋盘式细节的内存位图，执行编辑并检查输出像素。
// 参数：无。返回：输出与预期一致为 0；分配或绘制失败时为非零。
int main() {
    constexpr int width = 64;
    constexpr int height = 64;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    if (!bitmap || !pixels) return 1;
    HGDIOBJ previous = SelectObject(dc, bitmap);
    if (!previous || previous == HGDI_ERROR) return 2;
    auto* color = static_cast<unsigned int*>(pixels);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            color[y * width + x] = (x + y) % 2 ? 0x00FFFFFFU : 0x00000000U;

    ScreenshotEdits edits;
    edits.pixelated_areas.push_back({8, 8, 56, 56});
    edits.strokes.push_back({{{2, 2}, {6, 2}}});
    const bool applied = ApplyScreenshotEdits(dc, width, height, edits);
    const bool pixelated = color[20 * width + 20] == color[20 * width + 21];
    const bool pen_is_red = (color[2 * width + 4] & 0x00FFFFFFU) == 0x00F33A43U;
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    if (!applied) return 3;
    if (!pixelated) return 4;
    if (!pen_is_red) return 5;
    HDC text_dc = CreateCompatibleDC(nullptr);
    void* text_pixels = nullptr;
    info.bmiHeader.biWidth = 128;
    HBITMAP text_bitmap = text_dc ? CreateDIBSection(text_dc, &info, DIB_RGB_COLORS,
        &text_pixels, nullptr, 0) : nullptr;
    if (!text_bitmap || !text_pixels) return 6;
    HGDIOBJ old_text = SelectObject(text_dc, text_bitmap);
    if (!old_text || old_text == HGDI_ERROR) return 7;
    RECT background{0, 0, 128, height};
    FillRect(text_dc, &background, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    ScreenshotEdits text_edits;
    text_edits.texts.push_back({{8, 8}, L"文字", 24});
    const RECT text_bounds = desktop_orb::platform::ScreenshotTextBounds(text_dc,
        128, height, text_edits.texts.front());
    const bool text_applied = ApplyScreenshotEdits(text_dc, 128, height, text_edits);
    const auto* text_color = static_cast<const unsigned int*>(text_pixels);
    bool visible_text = false;
    for (int y = 8; y < text_bounds.bottom && !visible_text; ++y)
        for (int x = 8; x < text_bounds.right; ++x)
            if ((text_color[y * 128 + x] & 0x00FFFFFFU) != 0) { visible_text = true; break; }
    SelectObject(text_dc, old_text);
    DeleteObject(text_bitmap);
    DeleteDC(text_dc);
    if (text_bounds.right <= text_bounds.left || !text_applied || !visible_text) return 8;
    return 0;
}
