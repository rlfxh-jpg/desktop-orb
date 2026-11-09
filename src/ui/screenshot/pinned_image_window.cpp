// 文件职责：绘制可移动的截图贴图；右键关闭，保持在普通应用窗口之上。
// 位图与 HWND 均由当前对象持有，右键消息仅标记关闭，避免在窗口回调内销毁自身。
#include "desktop_orb/ui/screenshot/pinned_image_window.hpp"

#include <algorithm>

namespace desktop_orb::ui {

// 作用：释放贴图独占的 GDI 位图。参数：无。返回：无。
PinnedImageWindow::~PinnedImageWindow() {
    if (bitmap_) DeleteObject(bitmap_);
}

// 作用：按原始尺寸或屏幕工作区的七成尺寸显示截图。
// 参数：bitmap 为转移所有权的截图；position 为选区左上角屏幕坐标。
// 返回：窗口创建并显示时为 true；无效位图或创建失败时为 false。
bool PinnedImageWindow::Create(HBITMAP bitmap, POINT position) {
    bitmap_ = bitmap;
    BITMAP details{};
    if (!bitmap_ || !GetObjectW(bitmap_, sizeof(details), &details) ||
        details.bmWidth <= 0 || details.bmHeight <= 0) return false;
    MONITORINFO monitor{sizeof(monitor)};
    const HMONITOR nearest = MonitorFromPoint(position, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(nearest, &monitor)) return false;
    const int maximum_width = std::max(100,
        static_cast<int>(monitor.rcWork.right - monitor.rcWork.left) * 7 / 10);
    const int maximum_height = std::max(100,
        static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) * 7 / 10);
    const double scale = std::min({1.0, static_cast<double>(maximum_width) / details.bmWidth,
        static_cast<double>(maximum_height) / details.bmHeight});
    width_ = std::max(1, static_cast<int>(details.bmWidth * scale));
    height_ = std::max(1, static_cast<int>(details.bmHeight * scale));
    const int x = std::clamp(static_cast<int>(position.x), static_cast<int>(monitor.rcWork.left),
        std::max(static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - width_));
    const int y = std::clamp(static_cast<int>(position.y), static_cast<int>(monitor.rcWork.top),
        std::max(static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom) - height_));
    if (!CreateNative(L"DesktopOrbPinnedImage", L"桌面截图贴图", WS_POPUP,
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, x, y, width_, height_)) return false;
    ShowWindow(handle(), SW_SHOWNOACTIVATE);
    return true;
}

// 作用：绘制截图、响应拖动和关闭。
// 参数：message 为窗口消息；wparam、lparam 为消息附加值。
// 返回：处理结果；未处理时调用基类。
LRESULT PinnedImageWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(handle(), &paint);
            HDC source = CreateCompatibleDC(dc);
            if (source) {
                HGDIOBJ previous = SelectObject(source, bitmap_);
                BITMAP details{};
                if (previous && previous != HGDI_ERROR &&
                    GetObjectW(bitmap_, sizeof(details), &details)) {
                    SetStretchBltMode(dc, HALFTONE);
                    StretchBlt(dc, 0, 0, width_, height_, source, 0, 0,
                        details.bmWidth, details.bmHeight, SRCCOPY);
                    SelectObject(source, previous);
                }
                DeleteDC(source);
            }
            EndPaint(handle(), &paint);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // 核心逻辑：把客户区按标题栏交给系统拖动，跨显示器时仍使用原生窗口移动行为。
            ReleaseCapture();
            POINT cursor{};
            GetCursorPos(&cursor);
            SendMessageW(handle(), WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
            return 0;
        }
        case WM_RBUTTONUP:
            closed_ = true;
            ShowWindow(handle(), SW_HIDE);
            return 0;
        case WM_ERASEBKGND: return 1;
        default: return WindowBase::HandleMessage(message, wparam, lparam);
    }
}

}  // desktop_orb::ui 命名空间结束
