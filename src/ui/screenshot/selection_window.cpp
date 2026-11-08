// 文件职责：实现冻结桌面上的截图选区、尺寸提示和截后操作栏。
// 窗口覆盖虚拟屏幕坐标，借用平台快照；所有交互矩形统一使用屏幕坐标保存。
#include "desktop_orb/ui/screenshot/selection_window.hpp"

#include "desktop_orb/ui/common/drawing.hpp"
#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>
#include <string>
#include <windowsx.h>

namespace desktop_orb::ui {
namespace {

constexpr int kToolbarHeight = 42;
constexpr int kToolbarButtonWidth = 76;
constexpr int kToolbarGap = 5;
constexpr int kToolbarPadding = 6;
constexpr int kToolbarWidth = kToolbarPadding * 2 + kToolbarButtonWidth * 4 + kToolbarGap * 3;
constexpr int kSelectionBorderWidth = 3;
constexpr COLORREF kSelectionBorderColor = RGB(24, 132, 255);

// 作用：把屏幕坐标矩形转换为以虚拟桌面原点为基准的客户区坐标。
// 参数：screen 为屏幕矩形；origin 为窗口屏幕原点。返回：客户区矩形。
RECT ToClient(RECT screen, POINT origin) {
    OffsetRect(&screen, -origin.x, -origin.y);
    return screen;
}

// 作用：使用蓝色实心条带绘制选区四边，确保轮廓稳定可见。
// 参数：dc 为目标设备上下文；bounds 为选区客户区矩形。返回：无。
void DrawSelectionBorder(HDC dc, RECT bounds) {
    const int width = std::max(0, static_cast<int>(bounds.right - bounds.left));
    const int height = std::max(0, static_cast<int>(bounds.bottom - bounds.top));
    const int border_x = std::min(kSelectionBorderWidth, width);
    const int border_y = std::min(kSelectionBorderWidth, height);
    const int right = bounds.right - border_x;
    const int bottom = bounds.bottom - border_y;
    // 核心逻辑：四条实心边框均向选区内部绘制，避免坐标边界导致描边被裁掉。
    Fill(dc, {bounds.left, bounds.top, bounds.right, bounds.top + border_y},
         kSelectionBorderColor);
    Fill(dc, {bounds.left, bottom, bounds.right, bounds.bottom}, kSelectionBorderColor);
    Fill(dc, {bounds.left, bounds.top, bounds.left + border_x, bounds.bottom},
         kSelectionBorderColor);
    Fill(dc, {right, bounds.top, bounds.right, bounds.bottom}, kSelectionBorderColor);
}

}  // 匿名命名空间结束

bool SelectionWindow::Create() {
    return CreateNative(L"DesktopOrbScreenshotSelection", L"截图选区",
        WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 0, 0, 1, 1);
}

bool SelectionWindow::ShowFor(platform::ScreenshotService& snapshot) {
    snapshot_ = &snapshot;
    const RECT bounds = snapshot.bounds();
    if (!snapshot.ready() || !SetWindowPos(handle(), HWND_TOPMOST, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        snapshot_ = nullptr;
        return false;
    }
    dragging_ = false;
    selected_ = false;
    visible_ = true;
    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    InvalidateRect(handle(), nullptr, FALSE);
    SetForegroundWindow(handle());
    return true;
}

void SelectionWindow::Hide() {
    if (!visible_) return;
    visible_ = false;
    dragging_ = false;
    selected_ = false;
    snapshot_ = nullptr;
    ShowWindow(handle(), SW_HIDE);
}

POINT SelectionWindow::ScreenPoint(POINT point) const {
    POINT origin{};
    RECT window_bounds{};
    GetWindowRect(handle(), &window_bounds);
    origin = {window_bounds.left, window_bounds.top};
    POINT result{point.x + origin.x, point.y + origin.y};
    if (snapshot_) {
        const RECT desktop_bounds = snapshot_->bounds();
        result.x = std::clamp<LONG>(result.x, desktop_bounds.left, desktop_bounds.right);
        result.y = std::clamp<LONG>(result.y, desktop_bounds.top, desktop_bounds.bottom);
    }
    return result;
}

RECT SelectionWindow::Selection() const {
    return {std::min(anchor_.x, cursor_.x), std::min(anchor_.y, cursor_.y),
            std::max(anchor_.x, cursor_.x), std::max(anchor_.y, cursor_.y)};
}

RECT SelectionWindow::ToolbarBounds() const {
    const RECT selection = Selection();
    const RECT desktop = snapshot_ ? snapshot_->bounds() : RECT{};
    const UINT dpi = GetDpiForWindow(handle());
    const int width = platform::Scale(kToolbarWidth, dpi);
    const int height = platform::Scale(kToolbarHeight, dpi);
    const int gap = platform::Scale(10, dpi);
    int left = selection.left;
    int top = selection.bottom + gap;
    if (left + width > desktop.right) left = desktop.right - width;
    if (left < desktop.left) left = desktop.left;
    if (top + height > desktop.bottom) top = selection.top - height - gap;
    const int minimum_top = static_cast<int>(desktop.top);
    const int maximum_top = std::max(minimum_top, static_cast<int>(desktop.bottom) - height);
    top = std::clamp(top, minimum_top, maximum_top);
    return {left, top, left + width, top + height};
}

ScreenshotAction SelectionWindow::HitAction(POINT point) const {
    const RECT bounds = snapshot_ ? snapshot_->bounds() : RECT{};
    const RECT bar = ToClient(ToolbarBounds(), {bounds.left, bounds.top});
    const int dpi = static_cast<int>(GetDpiForWindow(handle()));
    const int pad = platform::Scale(kToolbarPadding, dpi);
    const int button = platform::Scale(kToolbarButtonWidth, dpi);
    const int gap = platform::Scale(kToolbarGap, dpi);
    const int offset = point.x - bar.left - pad;
    const int stride = button + gap;
    const int index = offset / stride;
    if (offset < 0 || index < 0 || index > 3 || offset % stride >= button) return ScreenshotAction::close;
    return static_cast<ScreenshotAction>(index);
}

void SelectionWindow::Dispatch(ScreenshotAction action) {
    if (on_action) on_action(action, Selection());
}

void SelectionWindow::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(handle(), &paint);
    RECT client{};
    GetClientRect(handle(), &client);
    const RECT desktop = snapshot_ ? snapshot_->bounds() : RECT{};
    const POINT origin{desktop.left, desktop.top};

    if (snapshot_ && snapshot_->ready()) {
        HDC source = CreateCompatibleDC(dc);
        if (source) {
            HGDIOBJ old = SelectObject(source, snapshot_->bitmap());
            // 核心逻辑：背景直接绘制启动选区前捕获的快照，避免活动窗口变化导致画面与选区不一致。
            if (old && old != HGDI_ERROR) {
                BitBlt(dc, 0, 0, client.right, client.bottom, source, 0, 0, SRCCOPY);
                SelectObject(source, old);
            }
            DeleteDC(source);
        }
    }

    // 核心逻辑：暗色遮罩只覆盖快照；完成选区后把选区区域重新从快照还原以突出真实像素。
    HDC shade_dc = CreateCompatibleDC(dc);
    HBITMAP shade_bitmap = shade_dc ? CreateCompatibleBitmap(dc, 1, 1) : nullptr;
    HGDIOBJ old_shade = shade_bitmap ? SelectObject(shade_dc, shade_bitmap) : nullptr;
    if (old_shade && old_shade != HGDI_ERROR) {
        RECT shade_area{0, 0, 1, 1};
        HBRUSH shade = CreateSolidBrush(RGB(226, 230, 233));
        FillRect(shade_dc, &shade_area, shade);
        DeleteObject(shade);
        // 核心逻辑：以浅灰色和约一半透明度覆盖选区外桌面，保留背景轮廓但突出当前操作区域。
        BLENDFUNCTION shade_blend{AC_SRC_OVER, 0, 128, 0};
        AlphaBlend(dc, 0, 0, client.right, client.bottom, shade_dc, 0, 0, 1, 1, shade_blend);
        SelectObject(shade_dc, old_shade);
    }
    if (shade_bitmap) DeleteObject(shade_bitmap);
    if (shade_dc) DeleteDC(shade_dc);
    HDC source = CreateCompatibleDC(dc);
    HGDIOBJ old_bitmap = source && snapshot_ && snapshot_->ready()
        ? SelectObject(source, snapshot_->bitmap()) : nullptr;
    if (old_bitmap && old_bitmap != HGDI_ERROR) {
        if (dragging_ || selected_) {
            const RECT rect = ToClient(Selection(), origin);
            // 核心逻辑：拖动期间也还原框内原始画面，使淡灰遮罩只作用于选区之外。
            BitBlt(dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                   source, rect.left, rect.top, SRCCOPY);
        }
        SelectObject(source, old_bitmap);
    }
    if (source) DeleteDC(source);

    const RECT selection = ToClient(Selection(), origin);
    if (dragging_ || selected_) {
        SetBkMode(dc, TRANSPARENT);
        DrawSelectionBorder(dc, selection);
        const std::wstring size = std::to_wstring(selection.right - selection.left) + L" × " +
                                  std::to_wstring(selection.bottom - selection.top);
        const int badge_width = platform::Scale(108, GetDpiForWindow(handle()));
        const int badge_height = platform::Scale(28, GetDpiForWindow(handle()));
        const int badge_gap = platform::Scale(6, GetDpiForWindow(handle()));
        RECT badge{Selection().left, Selection().top - badge_height - badge_gap,
                   Selection().left + badge_width, Selection().top - badge_gap};
        // 核心逻辑：尺寸提示贴着选区左上角；顶部空间不足时放到框内，避免跑出虚拟桌面。
        if (badge.top < desktop.top) {
            badge.top = Selection().top + badge_gap;
            badge.bottom = badge.top + badge_height;
        }
        const int badge_left = std::clamp(static_cast<int>(badge.left),
            static_cast<int>(desktop.left),
            std::max(static_cast<int>(desktop.left), static_cast<int>(desktop.right) - badge_width));
        OffsetRect(&badge, badge_left - badge.left, 0);
        badge = ToClient(badge, origin);
        Fill(dc, badge, Color(25, 34, 37));
        Text(dc, size, badge, platform::Scale(13, GetDpiForWindow(handle())), Color(255, 255, 255),
             FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (selected_) {
        const RECT bar = ToClient(ToolbarBounds(), origin);
        Fill(dc, bar, Color(29, 39, 42));
        const wchar_t* labels[] = {L"复制", L"保存", L"重新截取", L"关闭"};
        const UINT dpi = GetDpiForWindow(handle());
        const int pad = platform::Scale(kToolbarPadding, dpi);
        const int button = platform::Scale(kToolbarButtonWidth, dpi);
        const int height = platform::Scale(kToolbarHeight, dpi);
        const int gap = platform::Scale(kToolbarGap, dpi);
        for (int index = 0; index < 4; ++index) {
            RECT item{bar.left + pad + index * (button + gap), bar.top + platform::Scale(5, dpi),
                      bar.left + pad + index * (button + gap) + button, bar.top + height - platform::Scale(5, dpi)};
            Fill(dc, item, index == 3 ? Color(67, 76, 79) : Color(44, 135, 119));
            Text(dc, labels[index], item, platform::Scale(12, dpi), Color(255, 255, 255), FW_SEMIBOLD,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    EndPaint(handle(), &paint);
}

LRESULT SelectionWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: Paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_LBUTTONDOWN:
            if (!selected_) {
                anchor_ = cursor_ = ScreenPoint({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                dragging_ = true;
                SetCapture(handle());
                InvalidateRect(handle(), nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (dragging_) {
                cursor_ = ScreenPoint({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                InvalidateRect(handle(), nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) {
                cursor_ = ScreenPoint({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                dragging_ = false;
                ReleaseCapture();
                const RECT rect = Selection();
                selected_ = rect.right - rect.left >= 2 && rect.bottom - rect.top >= 2;
                InvalidateRect(handle(), nullptr, FALSE);
            } else if (selected_) {
                const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                const RECT desktop = snapshot_->bounds();
                const RECT bar = ToClient(ToolbarBounds(), {desktop.left, desktop.top});
                if (PtInRect(&bar, point)) Dispatch(HitAction(point));
                else {
                    const POINT origin{snapshot_->bounds().left, snapshot_->bounds().top};
                    const RECT selected_area = ToClient(Selection(), origin);
                    if (PtInRect(&selected_area, point)) return 0;
                    selected_ = false;
                    InvalidateRect(handle(), nullptr, FALSE);
                }
            }
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) Dispatch(ScreenshotAction::close);
            else if (wparam == VK_RETURN && selected_) Dispatch(ScreenshotAction::copy);
            return 0;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            if (snapshot_) {
                const RECT bounds = snapshot_->bounds();
                SetWindowPos(handle(), HWND_TOPMOST, bounds.left, bounds.top, bounds.right - bounds.left,
                             bounds.bottom - bounds.top, SWP_NOACTIVATE);
            }
            InvalidateRect(handle(), nullptr, FALSE);
            return 0;
        default: return WindowBase::HandleMessage(message, wparam, lparam);
    }
}

}  // desktop_orb::ui 命名空间结束
