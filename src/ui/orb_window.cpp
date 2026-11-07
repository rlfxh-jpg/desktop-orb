#include "desktop_orb/ui/orb_window.hpp"

#include "desktop_orb/platform/monitor.hpp"
#include "desktop_orb/ui/drawing.hpp"
#include <algorithm>

namespace desktop_orb::ui {
namespace {
constexpr UINT_PTR kTimer = 1;
constexpr int kLogicalSize = 58;
}

bool OrbWindow::Create(POINT initial) {
    if (!CreateNative(L"DesktopOrbMain", L"Desktop Orb", WS_POPUP,
                      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                      initial.x, initial.y, kLogicalSize, kLogicalSize)) return false;
    const int size = platform::Scale(kLogicalSize, GetDpiForWindow(handle()));
    SetWindowPos(handle(), HWND_TOPMOST, initial.x, initial.y, size, size, SWP_NOACTIVATE);
    UpdateRegion();
    SetAlpha(125);
    ShowWindow(handle(), SW_SHOWNOACTIVATE);
    SetTimer(handle(), kTimer, 120, nullptr);
    return true;
}

void OrbWindow::SetAlpha(BYTE alpha) {
    if (alpha_ == alpha) return;
    alpha_ = alpha;
    SetLayeredWindowAttributes(handle(), 0, alpha, LWA_ALPHA);
}

RECT OrbWindow::Bounds() const {
    RECT bounds{};
    GetWindowRect(handle(), &bounds);
    return bounds;
}

void OrbWindow::UpdateRegion() {
    const int size = platform::Scale(kLogicalSize, GetDpiForWindow(handle()));
    SetWindowRgn(handle(), CreateEllipticRgn(0, 0, size, size), TRUE);
    InvalidateRect(handle(), nullptr, FALSE);
}

void OrbWindow::SnapToEdge() {
    const RECT current = Bounds();
    const POINT center{(current.left + current.right) / 2, (current.top + current.bottom) / 2};
    const auto monitor = platform::MonitorAt(center);
    const int size = std::min({platform::Scale(kLogicalSize, GetDpiForWindow(handle())),
                               static_cast<int>(monitor.work.right - monitor.work.left),
                               static_cast<int>(monitor.work.bottom - monitor.work.top)});
    const int x = center.x < (monitor.work.left + monitor.work.right) / 2
        ? monitor.work.left : monitor.work.right - size;
    const int y = std::clamp(current.top, monitor.work.top, monitor.work.bottom - size);
    SetWindowPos(handle(), HWND_TOPMOST, x, y, size, size, SWP_NOACTIVATE);
    UpdateRegion();
    if (on_moved) on_moved({x, y});
}

void OrbWindow::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(handle(), &paint);
    RECT bounds{};
    GetClientRect(handle(), &bounds);
    const int size = bounds.right;
    Fill(dc, bounds, Color(27, 38, 44));
    HBRUSH accent = CreateSolidBrush(Color(47, 194, 164));
    HGDIOBJ old_brush = SelectObject(dc, accent);
    HPEN pen = CreatePen(PS_SOLID, 1, Color(47, 194, 164));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Ellipse(dc, size * 30 / 100, size * 30 / 100, size * 70 / 100, size * 70 / 100);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(accent);
    for (const POINT dot : {POINT{50, 12}, POINT{82, 50}, POINT{50, 82}, POINT{12, 50}}) {
        HBRUSH brush = CreateSolidBrush(Color(239, 247, 245));
        HGDIOBJ old = SelectObject(dc, brush);
        const int x = size * dot.x / 100;
        const int y = size * dot.y / 100;
        const int radius = std::max(2, size / 24);
        Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
        SelectObject(dc, old);
        DeleteObject(brush);
    }
    EndPaint(handle(), &paint);
}

LRESULT OrbWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: Paint(); return 0;
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

}  // namespace desktop_orb::ui
