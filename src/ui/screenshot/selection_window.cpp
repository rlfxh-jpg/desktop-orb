// 文件职责：实现冻结桌面上的截图选区、尺寸提示和截后操作栏。
// 窗口覆盖虚拟屏幕坐标，借用平台快照；所有交互矩形统一使用屏幕坐标保存。
#include "desktop_orb/ui/screenshot/selection_window.hpp"

#include "desktop_orb/ui/common/drawing.hpp"
#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <windowsx.h>

namespace desktop_orb::ui {
namespace {

constexpr int kToolbarHeight = 42;
constexpr int kToolbarButtonWidth = 76;
constexpr int kToolbarGap = 5;
constexpr int kToolbarPadding = 6;
constexpr int kToolbarButtonCount = 6;
constexpr int kToolbarWidth = kToolbarPadding * 2 + kToolbarButtonWidth * kToolbarButtonCount +
                              kToolbarGap * (kToolbarButtonCount - 1);
constexpr int kResultsWidth = 520;
constexpr int kResultsHeight = 260;
constexpr int kSelectionBorderWidth = 3;
constexpr int kResizeHitTolerance = 8;
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
    if (!CreateNative(L"DesktopOrbScreenshotSelection", L"截图选区",
            WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, 0, 0, 1, 1)) {
        return false;
    }
    if (!result_panel_.Create(handle())) {
        DestroyWindow(handle());
        return false;
    }
    result_panel_.on_copy = [this] { Dispatch(ScreenshotAction::copy_analysis_result); };
    return true;
}

SelectionWindow::~SelectionWindow() {
    if (frame_dc_ && previous_frame_bitmap_) SelectObject(frame_dc_, previous_frame_bitmap_);
    if (frame_bitmap_) DeleteObject(frame_bitmap_);
    if (frame_dc_) DeleteDC(frame_dc_);
}

bool SelectionWindow::ShowFor(platform::ScreenshotService& snapshot) {
    snapshot_ = &snapshot;
    const RECT bounds = snapshot.bounds();
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (!snapshot.ready() || !SetWindowPos(handle(), HWND_TOPMOST, bounds.left, bounds.top,
        width, height, SWP_NOACTIVATE) || !EnsureFrameSurface(width, height)) {
        snapshot_ = nullptr;
        return false;
    }
    drag_mode_ = DragMode::none;
    toolbar_pressed_ = false;
    selected_ = false;
    analysis_pending_ = false;
    analysis_result_visible_ = false;
    result_panel_.SetContent(L"", L"");
    result_panel_.Hide();
    visible_ = true;
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    // 核心逻辑：先在隐藏状态完成首帧合成并提交分层表面，避免系统先展示未绘制的全屏窗口。
    DrawFrame(frame_dc_);
    if (!PresentFrame()) {
        visible_ = false;
        snapshot_ = nullptr;
        return false;
    }
    ShowWindow(handle(), SW_SHOW);
    SetForegroundWindow(handle());
    return true;
}

bool SelectionWindow::EnsureFrameSurface(int width, int height) {
    if (frame_dc_ && frame_bitmap_ && frame_width_ == width && frame_height_ == height) return true;
    if (frame_dc_ && previous_frame_bitmap_) SelectObject(frame_dc_, previous_frame_bitmap_);
    if (frame_bitmap_) DeleteObject(frame_bitmap_);
    if (frame_dc_) DeleteDC(frame_dc_);
    frame_dc_ = nullptr;
    frame_bitmap_ = nullptr;
    previous_frame_bitmap_ = nullptr;
    frame_width_ = 0;
    frame_height_ = 0;

    frame_dc_ = CreateCompatibleDC(nullptr);
    if (!frame_dc_) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    frame_bitmap_ = CreateDIBSection(frame_dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!frame_bitmap_ || !pixels) {
        if (frame_bitmap_) DeleteObject(frame_bitmap_);
        DeleteDC(frame_dc_);
        frame_bitmap_ = nullptr;
        frame_dc_ = nullptr;
        return false;
    }
    previous_frame_bitmap_ = SelectObject(frame_dc_, frame_bitmap_);
    if (!previous_frame_bitmap_ || previous_frame_bitmap_ == HGDI_ERROR) {
        DeleteObject(frame_bitmap_);
        DeleteDC(frame_dc_);
        frame_bitmap_ = nullptr;
        frame_dc_ = nullptr;
        previous_frame_bitmap_ = nullptr;
        return false;
    }
    frame_width_ = width;
    frame_height_ = height;
    return true;
}

bool SelectionWindow::PresentFrame() {
    if (!frame_dc_ || !snapshot_) return false;
    POINT destination{snapshot_->bounds().left, snapshot_->bounds().top};
    POINT source{};
    SIZE size{frame_width_, frame_height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, 0};
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    const BOOL updated = UpdateLayeredWindow(handle(), screen, &destination, &size,
                                              frame_dc_, &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    return updated != FALSE;
}

void SelectionWindow::Hide() {
    if (!visible_) return;
    visible_ = false;
    drag_mode_ = DragMode::none;
    toolbar_pressed_ = false;
    selected_ = false;
    analysis_pending_ = false;
    analysis_result_visible_ = false;
    result_panel_.Hide();
    snapshot_ = nullptr;
    ShowWindow(handle(), SW_HIDE);
}

void SelectionWindow::SetAnalysisPending(bool pending) {
    analysis_pending_ = pending;
    if (pending) {
        analysis_result_visible_ = true;
        result_panel_.SetContent(L"分析中", L"正在分析截图，请稍候……");
    }
    LayoutResultPanel();
    InvalidateRect(handle(), nullptr, FALSE);
}

void SelectionWindow::SetAnalysisResult(std::wstring title, std::wstring text) {
    analysis_pending_ = false;
    analysis_result_visible_ = true;
    result_panel_.SetContent(std::move(title), std::move(text));
    LayoutResultPanel();
    InvalidateRect(handle(), nullptr, FALSE);
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
    return selection_;
}

unsigned int SelectionWindow::HitResizeEdges(POINT screen_point) const {
    if (!selected_) return 0;
    const int tolerance = platform::Scale(kResizeHitTolerance, GetDpiForWindow(handle()));
    const int horizontal_padding = tolerance * 2;
    const int vertical_padding = tolerance * 2;
    unsigned int edges = 0;

    if (screen_point.y >= selection_.top - vertical_padding &&
        screen_point.y <= selection_.bottom + vertical_padding) {
        const int left_distance = std::abs(static_cast<int>(screen_point.x - selection_.left));
        const int right_distance = std::abs(static_cast<int>(screen_point.x - selection_.right));
        if (left_distance <= tolerance && left_distance <= right_distance) edges |= resize_left;
        else if (right_distance <= tolerance) edges |= resize_right;
    }
    if (screen_point.x >= selection_.left - horizontal_padding &&
        screen_point.x <= selection_.right + horizontal_padding) {
        const int top_distance = std::abs(static_cast<int>(screen_point.y - selection_.top));
        const int bottom_distance = std::abs(static_cast<int>(screen_point.y - selection_.bottom));
        if (top_distance <= tolerance && top_distance <= bottom_distance) edges |= resize_top;
        else if (bottom_distance <= tolerance) edges |= resize_bottom;
    }
    return edges;
}

void SelectionWindow::UpdateCursor(POINT client_point) const {
    if (!snapshot_) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }

    const RECT desktop = snapshot_->bounds();
    const RECT toolbar = ToClient(ToolbarBounds(), {desktop.left, desktop.top});
    if (drag_mode_ == DragMode::none && selected_ && PtInRect(&toolbar, client_point)) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }
    if (drag_mode_ == DragMode::create) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
        return;
    }
    if (drag_mode_ == DragMode::move) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return;
    }

    const POINT screen_point = ScreenPoint(client_point);
    const unsigned int edges = drag_mode_ == DragMode::resize
        ? resize_edges_ : HitResizeEdges(screen_point);
    if (edges != 0) {
        const bool horizontal = (edges & (resize_left | resize_right)) != 0;
        const bool vertical = (edges & (resize_top | resize_bottom)) != 0;
        const bool diagonal_nw_se = ((edges & resize_left) && (edges & resize_top)) ||
                                    ((edges & resize_right) && (edges & resize_bottom));
        const wchar_t* cursor_id = horizontal && vertical
            ? (diagonal_nw_se ? IDC_SIZENWSE : IDC_SIZENESW)
            : (horizontal ? IDC_SIZEWE : IDC_SIZENS);
        SetCursor(LoadCursorW(nullptr, cursor_id));
        return;
    }

    if (selected_ && PtInRect(&selection_, screen_point)) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
    } else {
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    }
}

void SelectionWindow::UpdateDrag(POINT client_point) {
    const POINT current = ScreenPoint(client_point);
    const RECT desktop = snapshot_->bounds();
    if (drag_mode_ == DragMode::create) {
        selection_ = {std::min(drag_start_point_.x, current.x),
                      std::min(drag_start_point_.y, current.y),
                      std::max(drag_start_point_.x, current.x),
                      std::max(drag_start_point_.y, current.y)};
    } else if (drag_mode_ == DragMode::move) {
        const int width = drag_start_selection_.right - drag_start_selection_.left;
        const int height = drag_start_selection_.bottom - drag_start_selection_.top;
        const int max_left = std::max(static_cast<int>(desktop.left),
                                      static_cast<int>(desktop.right) - width);
        const int max_top = std::max(static_cast<int>(desktop.top),
                                     static_cast<int>(desktop.bottom) - height);
        const int left = std::clamp<int>(
            static_cast<int>(drag_start_selection_.left + current.x - drag_start_point_.x),
            static_cast<int>(desktop.left), max_left);
        const int top = std::clamp<int>(
            static_cast<int>(drag_start_selection_.top + current.y - drag_start_point_.y),
            static_cast<int>(desktop.top), max_top);
        selection_ = {left, top, left + width, top + height};
    } else if (drag_mode_ == DragMode::resize) {
        selection_ = drag_start_selection_;
        if (resize_edges_ & resize_left) {
            selection_.left = std::clamp<LONG>(current.x, desktop.left,
                                               selection_.right - 2);
        }
        if (resize_edges_ & resize_right) {
            selection_.right = std::clamp<LONG>(current.x, selection_.left + 2,
                                                desktop.right);
        }
        if (resize_edges_ & resize_top) {
            selection_.top = std::clamp<LONG>(current.y, desktop.top,
                                              selection_.bottom - 2);
        }
        if (resize_edges_ & resize_bottom) {
            selection_.bottom = std::clamp<LONG>(current.y, selection_.top + 2,
                                                 desktop.bottom);
        }
    }
    LayoutResultPanel();
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

RECT SelectionWindow::ResultsBounds() const {
    if (!snapshot_ || !selected_ || !analysis_result_visible_) return {};
    const RECT desktop = snapshot_->bounds();
    const UINT dpi = GetDpiForWindow(handle());
    const int desktop_width = static_cast<int>(desktop.right - desktop.left);
    const int desktop_height = static_cast<int>(desktop.bottom - desktop.top);
    const int width = std::min(platform::Scale(kResultsWidth, dpi), desktop_width);
    const int height = std::min(platform::Scale(kResultsHeight, dpi), desktop_height);
    const int gap = platform::Scale(6, dpi);
    const RECT toolbar = ToolbarBounds();
    const int left = std::clamp(static_cast<int>(toolbar.left), static_cast<int>(desktop.left),
                                static_cast<int>(desktop.right) - width);
    int top = toolbar.bottom + gap;
    if (top + height > desktop.bottom) top = toolbar.top - height - gap;
    top = std::clamp(top, static_cast<int>(desktop.top), static_cast<int>(desktop.bottom) - height);
    return {left, top, left + width, top + height};
}

void SelectionWindow::LayoutResultPanel() {
    if (!snapshot_) {
        result_panel_.Hide();
        return;
    }
    const RECT panel = ResultsBounds();
    if (!analysis_result_visible_ || panel.right <= panel.left || panel.bottom <= panel.top) {
        result_panel_.Hide();
        return;
    }
    result_panel_.ShowAt(panel);
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
    if (offset < 0 || index < 0 || index >= kToolbarButtonCount || offset % stride >= button)
        return ScreenshotAction::close;
    return static_cast<ScreenshotAction>(index);
}

void SelectionWindow::Dispatch(ScreenshotAction action) {
    if (on_action) on_action(action, Selection());
}

void SelectionWindow::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(handle(), &paint);
    if (frame_dc_ && snapshot_) {
        DrawFrame(frame_dc_);
        PresentFrame();
    }
    EndPaint(handle(), &paint);
}

void SelectionWindow::DrawFrame(HDC dc) {
    RECT client{0, 0, frame_width_, frame_height_};
    if (!dc || client.right <= 0 || client.bottom <= 0) return;
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
        if (drag_mode_ != DragMode::none || selected_) {
            const RECT rect = ToClient(Selection(), origin);
            // 核心逻辑：拖动期间也还原框内原始画面，使淡灰遮罩只作用于选区之外。
            BitBlt(dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                   source, rect.left, rect.top, SRCCOPY);
        }
        SelectObject(source, old_bitmap);
    }
    if (source) DeleteDC(source);

    const RECT selection = ToClient(Selection(), origin);
    if (drag_mode_ != DragMode::none || selected_) {
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
        const wchar_t* labels[] = {L"提取文字", L"总结截图", L"复制", L"保存", L"重新截取", L"关闭"};
        const UINT dpi = GetDpiForWindow(handle());
        const int pad = platform::Scale(kToolbarPadding, dpi);
        const int button = platform::Scale(kToolbarButtonWidth, dpi);
        const int height = platform::Scale(kToolbarHeight, dpi);
        const int gap = platform::Scale(kToolbarGap, dpi);
        for (int index = 0; index < kToolbarButtonCount; ++index) {
            RECT item{bar.left + pad + index * (button + gap), bar.top + platform::Scale(5, dpi),
                      bar.left + pad + index * (button + gap) + button, bar.top + height - platform::Scale(5, dpi)};
            const bool analysis_disabled = analysis_pending_ && index < 2;
            const COLORREF background = analysis_disabled ? Color(77, 84, 86)
                : (index == kToolbarButtonCount - 1 ? Color(67, 76, 79) : Color(44, 135, 119));
            Fill(dc, item, background);
            const wchar_t* label = analysis_disabled ? L"处理中" : labels[index];
            Text(dc, label, item, platform::Scale(12, dpi), Color(255, 255, 255), FW_SEMIBOLD,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

}

LRESULT SelectionWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: Paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(handle(), &point);
                UpdateCursor(point);
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return TRUE;
        case WM_LBUTTONDOWN:
        {
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const POINT screen_point = ScreenPoint(point);
            const RECT desktop = snapshot_ ? snapshot_->bounds() : RECT{};
            const RECT toolbar = ToClient(ToolbarBounds(), {desktop.left, desktop.top});
            if (selected_ && PtInRect(&toolbar, point)) {
                toolbar_pressed_ = true;
                SetCapture(handle());
                return 0;
            }

            drag_start_point_ = screen_point;
            if (selected_) {
                resize_edges_ = HitResizeEdges(screen_point);
                if (resize_edges_ != 0) {
                    drag_mode_ = DragMode::resize;
                    drag_start_selection_ = selection_;
                } else if (PtInRect(&selection_, screen_point)) {
                    drag_mode_ = DragMode::move;
                    drag_start_selection_ = selection_;
                } else {
                    selected_ = false;
                    resize_edges_ = 0;
                    analysis_result_visible_ = false;
                    result_panel_.SetContent(L"", L"");
                    result_panel_.Hide();
                    drag_mode_ = DragMode::create;
                    selection_ = {screen_point.x, screen_point.y, screen_point.x, screen_point.y};
                }
            } else {
                drag_mode_ = DragMode::create;
                selection_ = {screen_point.x, screen_point.y, screen_point.x, screen_point.y};
            }
            SetCapture(handle());
            UpdateCursor(point);
            InvalidateRect(handle(), nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (drag_mode_ != DragMode::none) {
                UpdateDrag({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                InvalidateRect(handle(), nullptr, FALSE);
            }
            UpdateCursor({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            return 0;
        case WM_LBUTTONUP:
            if (toolbar_pressed_) {
                toolbar_pressed_ = false;
                ReleaseCapture();
                const RECT desktop = snapshot_ ? snapshot_->bounds() : RECT{};
                const RECT toolbar = ToClient(ToolbarBounds(), {desktop.left, desktop.top});
                const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (PtInRect(&toolbar, point)) Dispatch(HitAction(point));
                return 0;
            }
            if (drag_mode_ != DragMode::none) {
                UpdateDrag({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                const DragMode completed_mode = drag_mode_;
                drag_mode_ = DragMode::none;
                resize_edges_ = 0;
                ReleaseCapture();
                if (completed_mode == DragMode::create) {
                    selected_ = selection_.right - selection_.left >= 2 &&
                                selection_.bottom - selection_.top >= 2;
                }
                UpdateCursor({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                InvalidateRect(handle(), nullptr, FALSE);
            }
            return 0;
        case WM_CAPTURECHANGED:
            drag_mode_ = DragMode::none;
            toolbar_pressed_ = false;
            resize_edges_ = 0;
            InvalidateRect(handle(), nullptr, FALSE);
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
            LayoutResultPanel();
            InvalidateRect(handle(), nullptr, FALSE);
            return 0;
        default: return WindowBase::HandleMessage(message, wparam, lparam);
    }
}

}  // desktop_orb::ui 命名空间结束
