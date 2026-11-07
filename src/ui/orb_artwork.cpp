// 文件职责：把已解码的悬浮球图标写入自上而下的 DIB，再提交给 Win32 分层窗口。
// 调用方拥有 HWND；本文件只临时创建并释放绘制所需的 GDI 对象。
#include "desktop_orb/ui/orb_artwork.hpp"

namespace desktop_orb::ui {

bool OrbArtwork::Render(HWND window, BYTE opacity) const {
    if (!ready() || !window) return false;
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return false;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return false;

    BITMAPINFO info{};
    // 核心逻辑：负高度让 DIB 自上而下排列，与 IconImage 的 BGRA 输出一致，
    // 否则悬浮球图像会上下颠倒。
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC memory = CreateCompatibleDC(screen);
    void* pixels = nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    bool success = false;
    if (bitmap && pixels && image_.Rasterize(width, height, pixels, width * 4)) {
        // 核心逻辑：AC_SRC_ALPHA 使用每个像素的透明度；opacity 再统一控制
        // 悬浮球闲置和悬停时的整体透明度。
        HGDIOBJ previous = SelectObject(memory, bitmap);
        POINT position{bounds.left, bounds.top};
        POINT origin{0, 0};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
        success = UpdateLayeredWindow(window, screen, &position, &size, memory,
                                      &origin, 0, &blend, ULW_ALPHA) != FALSE;
        SelectObject(memory, previous);
    }
    // 核心逻辑：UpdateLayeredWindow 会复制像素，因此提交后可立即释放位图和 DC；
    // 失败路径也走同一段清理，避免泄漏 GDI 句柄。
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return success;
}

}  // desktop_orb::ui 命名空间结束
