// 文件职责：控制悬浮球的置顶窗口、拖动吸边、透明度和输入事件。
// 图标像素由 OrbArtwork 绘制，功能菜单及面板状态由 App 回调协调。
#include "desktop_orb/ui/common/orb_window.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>

namespace desktop_orb::ui {
namespace {
constexpr UINT_PTR kTimer = 1;
constexpr int kLogicalSize = 58;
constexpr int kEdgeInset = 24;
}

bool OrbWindow::Create(POINT initial) {
    if (!artwork_.ready()) return false;
    if (!CreateNative(L"DesktopOrbMain", L"Desktop Orb", WS_POPUP,
                      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                      initial.x, initial.y, kLogicalSize, kLogicalSize)) return false;
    const int size = platform::Scale(kLogicalSize, GetDpiForWindow(handle()));
    SetWindowPos(handle(), HWND_TOPMOST, initial.x, initial.y, size, size, SWP_NOACTIVATE);
    alpha_ = 125;
    if (!artwork_.Render(handle(), alpha_)) return false;
    ShowWindow(handle(), SW_SHOWNOACTIVATE);
    SetTimer(handle(), kTimer, 120, nullptr);
    return true;
}

void OrbWindow::SetAlpha(BYTE alpha) {
    if (alpha_ == alpha) return;
    if (artwork_.Render(handle(), alpha)) alpha_ = alpha;
}

RECT OrbWindow::Bounds() const {
    RECT bounds{};
    GetWindowRect(handle(), &bounds);
    return bounds;
}

void OrbWindow::SnapToEdge() {
    const RECT current = Bounds();
    const POINT center{(current.left + current.right) / 2, (current.top + current.bottom) / 2};
    const auto monitor = platform::MonitorAt(center);
    const int work_width = monitor.work.right - monitor.work.left;
    const int work_height = monitor.work.bottom - monitor.work.top;
    const int size = std::min({platform::Scale(kLogicalSize, GetDpiForWindow(handle())),
                               work_width, work_height});
    // 核心逻辑：吸边时按显示器 DPI 留出 24 DIP；极窄工作区则收缩留白，
    // 确保悬浮球始终完整位于当前显示器的可用区域内。
    const int inset = std::max(0, std::min({platform::Scale(kEdgeInset, monitor.dpi ? monitor.dpi : 96),
        (work_width - size) / 2, (work_height - size) / 2}));
    const int x = center.x < (monitor.work.left + monitor.work.right) / 2
        ? monitor.work.left + inset : monitor.work.right - size - inset;
    const int y = std::clamp(current.top, monitor.work.top + inset,
        monitor.work.bottom - size - inset);
    SetWindowPos(handle(), HWND_TOPMOST, x, y, size, size, SWP_NOACTIVATE);
    artwork_.Render(handle(), alpha_);
    if (on_moved) on_moved({x, y});
}

LRESULT OrbWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        // 核心逻辑：UpdateLayeredWindow 已直接提交像素，此处只清除绘制请求。
        case WM_PAINT: ValidateRect(handle(), nullptr); return 0;
        case WM_LBUTTONDOWN:
            GetCursorPos(&drag_cursor_);
            drag_origin_ = Bounds();
            dragging_ = true;
            moved_ = false;
            SetCapture(handle());
            return 0;
        case WM_MOUSEMOVE:
            if (dragging_) {
                POINT point{};
                GetCursorPos(&point);
                const int dx = point.x - drag_cursor_.x;
                const int dy = point.y - drag_cursor_.y;
                if (std::abs(dx) > 4 || std::abs(dy) > 4) {
                    if (!moved_ && on_drag_start) on_drag_start();
                    moved_ = true;
                }
                if (moved_) SetWindowPos(handle(), HWND_TOPMOST, drag_origin_.left + dx,
                                         drag_origin_.top + dy, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
                if (moved_) SnapToEdge();
                else if (on_click) on_click();
            }
            return 0;
        case WM_CAPTURECHANGED: dragging_ = false; return 0;
        case WM_RBUTTONUP: if (on_context) on_context(); return 0;
        case WM_TIMER: if (wparam == kTimer && on_tick) on_tick(); return 0;
        case WM_HOTKEY: if (on_hotkey) on_hotkey(); return 0;
        case WM_CLIPBOARDUPDATE: if (on_clipboard_changed) on_clipboard_changed(); return 0;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            SnapToEdge();
            return 0;
        case WM_DESTROY:
            KillTimer(handle(), kTimer);
            PostQuitMessage(0);
            return 0;
        default: return WindowBase::HandleMessage(message, wparam, lparam);
    }
}

}  // desktop_orb::ui 命名空间结束
