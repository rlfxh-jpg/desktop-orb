// 文件职责：实现冻结桌面上的截图选区、尺寸提示和截后操作栏。
// 窗口覆盖虚拟屏幕坐标，借用平台快照；所有交互矩形统一使用屏幕坐标保存。
#include "desktop_orb/ui/screenshot/selection_window.hpp"

#include "desktop_orb/ui/common/drawing.hpp"
#include "desktop_orb/ui/screenshot/toolbar_icons.hpp"
#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <utility>
#include <windowsx.h>

namespace desktop_orb::ui {
namespace {

constexpr int kToolbarHeight = 44;
constexpr int kToolbarButtonWidth = 36;
constexpr int kToolbarGap = 4;
constexpr int kToolbarPadding = 6;
constexpr int kToolbarButtonCount = 14;
constexpr int kResultsWidth = 520;
constexpr int kResultsHeight = 260;
constexpr int kSelectionBorderWidth = 3;
constexpr int kResizeHitTolerance = 8;
constexpr COLORREF kSelectionBorderColor = RGB(24, 132, 255);
constexpr UINT_PTR kResultPanelRestoreTimerId = 1;
constexpr UINT kResultPanelRestoreDelayMs = 1000;
constexpr UINT_PTR kToolbarTooltipTimerId = 2;
constexpr UINT kToolbarTooltipDelayMs = 650;
constexpr UINT kFinishTextInputMessage = WM_APP + 42;
constexpr int kTextFontHeight = 24;
constexpr std::array<ScreenshotAction, kToolbarButtonCount> kToolbarActions{
    ScreenshotAction::extract_text, ScreenshotAction::summarize,
    ScreenshotAction::translate, ScreenshotAction::copy, ScreenshotAction::save,
    ScreenshotAction::recapture, ScreenshotAction::pin_image,
    ScreenshotAction::pen, ScreenshotAction::add_text, ScreenshotAction::pixelate,
    ScreenshotAction::eraser,
    ScreenshotAction::undo, ScreenshotAction::redo, ScreenshotAction::close};
constexpr std::array<const wchar_t*, kToolbarButtonCount> kToolbarNames{
    L"提取文字", L"总结截图", L"翻译文字", L"复制", L"保存",
    L"重新截取", L"贴到桌面", L"画笔", L"输入文字", L"马赛克", L"橡皮擦",
    L"撤销", L"重做", L"关闭"};

// 作用：集中保存工具栏的物理像素尺寸，保证绘制、点击和悬停使用同一排版。
// 参数：无。返回：无。
struct ToolbarLayout {
    int padding = 0;
    int button = 0;
    int gap = 0;
    int height = 0;
    int width = 0;
};

// 作用：在当前 DPI 下计算单排按钮尺寸，屏幕较窄时均匀收紧按钮和间隙。
// 参数：dpi 为窗口 DPI；available_width 为当前显示器宽度。返回：工具栏物理像素布局。
ToolbarLayout MakeToolbarLayout(UINT dpi, int available_width) {
    ToolbarLayout layout{platform::Scale(kToolbarPadding, dpi),
        platform::Scale(kToolbarButtonWidth, dpi), platform::Scale(kToolbarGap, dpi),
        platform::Scale(kToolbarHeight, dpi), 0};
    layout.width = layout.padding * 2 + layout.button * kToolbarButtonCount +
        layout.gap * (kToolbarButtonCount - 1);
    if (layout.width > available_width && available_width > 0) {
        layout.padding = std::max(1, std::min(layout.padding, available_width / 80));
        layout.gap = std::max(1, std::min(layout.gap, available_width / 160));
        layout.button = std::max(1, (available_width - layout.padding * 2 -
            layout.gap * (kToolbarButtonCount - 1)) / kToolbarButtonCount);
        layout.width = layout.padding * 2 + layout.button * kToolbarButtonCount +
            layout.gap * (kToolbarButtonCount - 1);
    }
    return layout;
}

// 作用：选择工具栏应停留的显示器，避免单排按钮跨越显示器边界或落到屏幕空隙。
// 参数：selection 为屏幕坐标选区。返回：选区左下角所在或最近的显示器。
platform::Monitor ToolbarMonitor(RECT selection) {
    return platform::MonitorAt({selection.left, selection.bottom - 1});
}

// 作用：判断画笔笔迹是否接触橡皮擦的圆形范围。
// 参数：stroke 为相对选区的笔迹；point 为相对选区的擦除点；radius 为半径。
// 返回：至少一个线段接触擦除点时为 true。
bool StrokeTouches(const platform::ScreenshotStroke& stroke, POINT point, int radius) {
    if (stroke.points.empty()) return false;
    const double radius_squared = static_cast<double>(radius) * radius;
    for (size_t index = 0; index < stroke.points.size(); ++index) {
        const POINT first = stroke.points[index];
        const POINT second = stroke.points[std::min(index + 1, stroke.points.size() - 1)];
        const double dx = static_cast<double>(second.x - first.x);
        const double dy = static_cast<double>(second.y - first.y);
        const double length_squared = dx * dx + dy * dy;
        const double position = length_squared == 0 ? 0 : std::clamp(
            ((point.x - first.x) * dx + (point.y - first.y) * dy) / length_squared, 0.0, 1.0);
        const double nearest_x = first.x + position * dx;
        const double nearest_y = first.y + position * dy;
        const double distance_x = point.x - nearest_x;
        const double distance_y = point.y - nearest_y;
        if (distance_x * distance_x + distance_y * distance_y <= radius_squared) return true;
    }
    return false;
}

// 作用：判断马赛克矩形是否接触橡皮擦圆形范围。
// 参数：area 为相对选区矩形；point 为相对选区的擦除点；radius 为半径。
// 返回：两者接触时为 true。
bool AreaTouches(RECT area, POINT point, int radius) {
    const int x = std::clamp(static_cast<int>(point.x), static_cast<int>(area.left),
        static_cast<int>(area.right));
    const int y = std::clamp(static_cast<int>(point.y), static_cast<int>(area.top),
        static_cast<int>(area.bottom));
    const long long dx = point.x - x;
    const long long dy = point.y - y;
    return dx * dx + dy * dy <= static_cast<long long>(radius) * radius;
}

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
    FinishTextInput(false, false);
    if (frame_dc_ && previous_frame_bitmap_) SelectObject(frame_dc_, previous_frame_bitmap_);
    if (frame_bitmap_) DeleteObject(frame_bitmap_);
    if (frame_dc_) DeleteDC(frame_dc_);
}

bool SelectionWindow::ShowFor(platform::ScreenshotService& snapshot) {
    FinishTextInput(false, false);
    KillTimer(handle(), kResultPanelRestoreTimerId);
    ClearToolbarHover();
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
    edits_ = {};
    history_.Reset();
    edit_tool_ = EditTool::none;
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
    FinishTextInput(false, false);
    if (!visible_) return;
    KillTimer(handle(), kResultPanelRestoreTimerId);
    ClearToolbarHover();
    visible_ = false;
    drag_mode_ = DragMode::none;
    toolbar_pressed_ = false;
    selected_ = false;
    edits_ = {};
    history_.Reset();
    edit_tool_ = EditTool::none;
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
    const POINT edit_point = ScreenPoint(client_point);
    if (selected_ && HitResizeEdges(edit_point) == 0 &&
        PtInRect(&selection_, edit_point) && edit_tool_ != EditTool::none) {
        SetCursor(LoadCursorW(nullptr, edit_tool_ == EditTool::text ? IDC_IBEAM : IDC_CROSS));
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
    const RECT previous_selection = selection_;
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
        // 核心逻辑：编辑坐标相对选区左上角；拉伸左边或上边时平移编辑坐标，
        // 让标注继续覆盖其原来的屏幕像素位置。
        const int shift_x = previous_selection.left - selection_.left;
        const int shift_y = previous_selection.top - selection_.top;
        for (auto& stroke : edits_.strokes)
            for (auto& point : stroke.points) { point.x += shift_x; point.y += shift_y; }
        for (auto& area : edits_.pixelated_areas) OffsetRect(&area, shift_x, shift_y);
        for (auto& annotation : edits_.texts) {
            annotation.origin.x += shift_x;
            annotation.origin.y += shift_y;
        }
    }
    if (analysis_result_visible_) {
        // 核心逻辑：选区移动期间隐藏结果面板，并在每次鼠标移动后重新等待一秒。
        result_panel_.Hide();
        SetTimer(handle(), kResultPanelRestoreTimerId, kResultPanelRestoreDelayMs, nullptr);
    }
}

RECT SelectionWindow::ToolbarBounds() const {
    const RECT selection = Selection();
    const RECT desktop = snapshot_ ? ToolbarMonitor(selection).bounds : RECT{};
    const UINT dpi = GetDpiForWindow(handle());
    const auto layout = MakeToolbarLayout(dpi,
        static_cast<int>(desktop.right - desktop.left));
    const int width = layout.width;
    const int height = layout.height;
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
    const RECT selection = Selection();
    const RECT toolbar = ToolbarBounds();
    const auto fits = [&](RECT candidate) {
        const bool inside_desktop = candidate.left >= desktop.left &&
            candidate.top >= desktop.top && candidate.right <= desktop.right &&
            candidate.bottom <= desktop.bottom;
        const bool overlaps_selection = candidate.left < selection.right &&
            candidate.right > selection.left && candidate.top < selection.bottom &&
            candidate.bottom > selection.top;
        const bool overlaps_toolbar = candidate.left < toolbar.right &&
            candidate.right > toolbar.left && candidate.top < toolbar.bottom &&
            candidate.bottom > toolbar.top;
        return inside_desktop && !overlaps_selection && !overlaps_toolbar;
    };
    const int left = std::clamp(static_cast<int>(toolbar.left),
                                static_cast<int>(desktop.left),
                                static_cast<int>(desktop.right) - width);
    const RECT below_toolbar{left, toolbar.bottom + gap,
                             left + width, toolbar.bottom + gap + height};
    if (fits(below_toolbar)) return below_toolbar;

    // 核心逻辑：操作栏下方空间不足时，把结果面板放到截图选区上方，避免遮住截图像素。
    const int selection_left = std::clamp(static_cast<int>(selection.left),
        static_cast<int>(desktop.left), static_cast<int>(desktop.right) - width);
    const RECT above_selection{selection_left, selection.top - gap - height,
                               selection_left + width, selection.top - gap};
    if (fits(above_selection)) return above_selection;

    const RECT left_of_selection{selection.left - gap - width, selection.top,
                                 selection.left - gap, selection.top + height};
    if (fits(left_of_selection)) return left_of_selection;
    const RECT right_of_selection{selection.right + gap, selection.top,
                                  selection.right + gap + width, selection.top + height};
    if (fits(right_of_selection)) return right_of_selection;

    return {};
}

void SelectionWindow::LayoutResultPanel() {
    if (!snapshot_ || drag_mode_ != DragMode::none) {
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
    const RECT monitor = ToolbarMonitor(Selection()).bounds;
    const auto layout = MakeToolbarLayout(GetDpiForWindow(handle()),
        static_cast<int>(monitor.right - monitor.left));
    const int offset = point.x - bar.left - layout.padding;
    const int stride = layout.button + layout.gap;
    if (offset < 0 || point.y < bar.top + (layout.height - layout.button) / 2 ||
        point.y >= bar.top + (layout.height + layout.button) / 2)
        return ScreenshotAction::none;
    const int index = offset / stride;
    if (index >= kToolbarButtonCount || offset % stride >= layout.button)
        return ScreenshotAction::none;
    return kToolbarActions[index];
}

// 作用：追踪当前图标的鼠标悬停，只有连续停留一段时间才显示名称。
// 参数：point 为窗口客户区坐标。返回：无；拖拽与按钮按下期间不显示提示。
void SelectionWindow::UpdateToolbarHover(POINT point) {
    const ScreenshotAction next = selected_ && drag_mode_ == DragMode::none && !toolbar_pressed_
        ? HitAction(point) : ScreenshotAction::none;
    if (next == hovered_action_) return;
    ClearToolbarHover();
    hovered_action_ = next;
    if (next != ScreenshotAction::none)
        SetTimer(handle(), kToolbarTooltipTimerId, kToolbarTooltipDelayMs, nullptr);
}

// 作用：清除工具栏提示计时器和当前悬停状态。
// 参数：无。返回：无。
void SelectionWindow::ClearToolbarHover() {
    KillTimer(handle(), kToolbarTooltipTimerId);
    const bool had_tooltip = toolbar_tooltip_visible_;
    hovered_action_ = ScreenshotAction::none;
    toolbar_tooltip_visible_ = false;
    if (had_tooltip && visible_) InvalidateRect(handle(), nullptr, FALSE);
}

// 作用：复制当前选区与编辑内容，供一次完整手势结束后写入撤销栈。
// 参数：无。返回：不借用窗口或位图资源的状态副本。
ScreenshotSelectionState SelectionWindow::CaptureState() const {
    return {selection_, selected_, edits_};
}

// 作用：只记录实际改变了选区或编辑对象的已完成操作。
// 参数：before 为操作开始前的状态。返回：无；新操作会清除重做分支。
void SelectionWindow::CommitState(const ScreenshotSelectionState& before) {
    // 核心逻辑：初次创建选区不加入历史；否则撤销后工具栏消失，用户无法点击重做。
    if (!before.selected) return;
    if (!history_.Commit(before, CaptureState())) return;
    KillTimer(handle(), kResultPanelRestoreTimerId);
    analysis_result_visible_ = false;
    result_panel_.Hide();
    result_panel_.SetContent(L"", L"");
    InvalidateRect(handle(), nullptr, FALSE);
}

// 作用：切换到历史中的选区和编辑状态，并清除已不对应的分析结果。
// 参数：state 为保存的完整状态。返回：无。
void SelectionWindow::RestoreState(const ScreenshotSelectionState& state) {
    ClearToolbarHover();
    selection_ = state.selection;
    selected_ = state.selected;
    edits_ = state.edits;
    if (!selected_) edit_tool_ = EditTool::none;
    KillTimer(handle(), kResultPanelRestoreTimerId);
    analysis_result_visible_ = false;
    result_panel_.Hide();
    result_panel_.SetContent(L"", L"");
    InvalidateRect(handle(), nullptr, FALSE);
}

// 作用：退回最近一次已完成的选区调整或标注操作。
// 参数：无。返回：无；撤销栈为空时不改变画面。
void SelectionWindow::Undo() {
    ScreenshotSelectionState previous;
    if (!history_.Undo(CaptureState(), previous)) return;
    RestoreState(previous);
}

// 作用：恢复最近一次被撤销的操作。
// 参数：无。返回：无；重做栈为空时不改变画面。
void SelectionWindow::Redo() {
    ScreenshotSelectionState next;
    if (!history_.Redo(CaptureState(), next)) return;
    RestoreState(next);
}

// 作用：沿手势路径采样并删除相交的笔迹或马赛克对象。
// 参数：from、to 为屏幕坐标的路径两端。返回：至少删除一个对象时为 true。
bool SelectionWindow::EraseBetween(POINT from, POINT to) {
    if (edits_.strokes.empty() && edits_.pixelated_areas.empty() && edits_.texts.empty()) return false;
    const int radius = std::max(4, platform::Scale(9, GetDpiForWindow(handle())));
    const int distance = std::max(std::abs(static_cast<int>(to.x - from.x)),
        std::abs(static_cast<int>(to.y - from.y)));
    const int steps = std::max(1, distance / std::max(1, radius / 2));
    bool changed = false;
    for (int step = 0; step <= steps; ++step) {
        const POINT relative{from.x + (to.x - from.x) * step / steps - selection_.left,
            from.y + (to.y - from.y) * step / steps - selection_.top};
        changed |= std::erase_if(edits_.strokes,
            [&](const auto& stroke) { return StrokeTouches(stroke, relative, radius); }) != 0;
        changed |= std::erase_if(edits_.pixelated_areas,
            [&](RECT area) { return AreaTouches(area, relative, radius); }) != 0;
        changed |= std::erase_if(edits_.texts, [&](const auto& annotation) {
            const RECT bounds = platform::ScreenshotTextBounds(frame_dc_,
                selection_.right - selection_.left, selection_.bottom - selection_.top, annotation);
            return bounds.right > bounds.left && AreaTouches(bounds, relative, radius);
        }) != 0;
        if (edits_.strokes.empty() && edits_.pixelated_areas.empty() && edits_.texts.empty()) break;
    }
    return changed;
}

// 作用：在文字落点附近创建由本窗口拥有的临时原生输入框。
// 参数：screen_point 为选区内的屏幕坐标。返回：创建并取得输入焦点为 true。
bool SelectionWindow::BeginTextInput(POINT screen_point) {
    if (!selected_ || text_editor_) return false;
    const RECT desktop = snapshot_ ? snapshot_->bounds() : selection_;
    const int width = std::min(static_cast<int>(desktop.right - desktop.left),
        std::max(60, std::min(360, static_cast<int>(selection_.right - screen_point.x))));
    const int height = std::min(static_cast<int>(desktop.bottom - desktop.top),
        std::max(38, std::min(120, static_cast<int>(selection_.bottom - screen_point.y))));
    const int left = std::clamp(static_cast<int>(screen_point.x), static_cast<int>(desktop.left),
        static_cast<int>(desktop.right) - width);
    const int top = std::clamp(static_cast<int>(screen_point.y), static_cast<int>(desktop.top),
        static_cast<int>(desktop.bottom) - height);
    text_origin_ = {screen_point.x - selection_.left, screen_point.y - selection_.top};
    HWND editor = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"EDIT", L"",
        WS_POPUP | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL,
        left, top, width, height, handle(), nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!editor) return false;
    text_editor_ = editor;
    SetWindowLongPtrW(editor, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    original_text_edit_proc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(editor, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&TextEditProc)));
    if (!original_text_edit_proc_) {
        text_editor_ = nullptr;
        DestroyWindow(editor);
        return false;
    }
    SendMessageW(editor, EM_LIMITTEXT, 2048, 0);
    text_editor_font_ = CreateFontW(-platform::Scale(kTextFontHeight, GetDpiForWindow(handle())),
        0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    SendMessageW(editor, WM_SETFONT,
        reinterpret_cast<WPARAM>(text_editor_font_ ? text_editor_font_ : GetStockObject(DEFAULT_GUI_FONT)),
        TRUE);
    // 核心逻辑：输入框单独接受键盘焦点，不被分层窗口每帧重绘覆盖；确认后才合成到截图像素。
    ShowWindow(editor, SW_SHOW);
    SetFocus(editor);
    return true;
}

// 作用：提交或取消当前文字输入，并释放窗口及字体资源。
// 参数：accept 表示提交非空文字；restore_focus 表示操作后继续使用截图快捷键。返回：无。
void SelectionWindow::FinishTextInput(bool accept, bool restore_focus) {
    if (!text_editor_) return;
    HWND editor = text_editor_;
    std::wstring content;
    if (accept) {
        const int length = GetWindowTextLengthW(editor);
        content.resize(length + 1);
        const int copied = GetWindowTextW(editor, content.data(), length + 1);
        content.resize(std::max(0, copied));
    }
    text_editor_ = nullptr;
    DestroyWindow(editor);
    original_text_edit_proc_ = nullptr;
    if (text_editor_font_) DeleteObject(text_editor_font_);
    text_editor_font_ = nullptr;
    if (accept && content.find_first_not_of(L" \t\r\n") != std::wstring::npos && selected_) {
        const ScreenshotSelectionState before = CaptureState();
        edits_.texts.push_back({text_origin_, std::move(content),
            platform::Scale(kTextFontHeight, GetDpiForWindow(handle()))});
        CommitState(before);
        InvalidateRect(handle(), nullptr, FALSE);
    }
    if (restore_focus && visible_) SetFocus(handle());
}

// 作用：让 Enter 确认、Esc 取消文字输入，Shift+Enter 保留原生多行输入。
// 参数：editor 为当前输入框；message、wparam、lparam 为消息。返回：处理结果或原过程结果。
LRESULT CALLBACK SelectionWindow::TextEditProc(HWND editor, UINT message,
    WPARAM wparam, LPARAM lparam) {
    auto* owner = reinterpret_cast<SelectionWindow*>(GetWindowLongPtrW(editor, GWLP_USERDATA));
    if (!owner || !owner->original_text_edit_proc_)
        return DefWindowProcW(editor, message, wparam, lparam);
    if (message == WM_KEYDOWN) {
        if (wparam == VK_ESCAPE || (wparam == VK_RETURN && !(GetKeyState(VK_SHIFT) & 0x8000))) {
            owner->FinishTextInput(wparam == VK_RETURN);
            return 0;
        }
    }
    const WNDPROC original = owner->original_text_edit_proc_;
    const LRESULT result = CallWindowProcW(original, editor, message, wparam, lparam);
    if (message == WM_KILLFOCUS) PostMessageW(owner->handle(), kFinishTextInputMessage, 0, 0);
    return result;
}

void SelectionWindow::Dispatch(ScreenshotAction action) {
    if (action == ScreenshotAction::none) return;
    if (text_editor_) FinishTextInput(true, false);
    if (drag_mode_ != DragMode::none &&
        (action == ScreenshotAction::undo || action == ScreenshotAction::redo)) return;
    if (action == ScreenshotAction::undo) { Undo(); return; }
    if (action == ScreenshotAction::redo) { Redo(); return; }
    if (action == ScreenshotAction::pen || action == ScreenshotAction::pixelate ||
        action == ScreenshotAction::eraser || action == ScreenshotAction::add_text) {
        const EditTool requested = action == ScreenshotAction::pen ? EditTool::pen
            : action == ScreenshotAction::pixelate ? EditTool::pixelate
            : action == ScreenshotAction::eraser ? EditTool::eraser : EditTool::text;
        edit_tool_ = edit_tool_ == requested ? EditTool::none : requested;
        InvalidateRect(handle(), nullptr, FALSE);
        return;
    }
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

    if (selected_ && (!edits_.strokes.empty() || !edits_.pixelated_areas.empty() ||
        !edits_.texts.empty())) {
        const int saved = SaveDC(dc);
        const RECT region = ToClient(Selection(), origin);
        IntersectClipRect(dc, region.left, region.top, region.right, region.bottom);
        SetViewportOrgEx(dc, region.left, region.top, nullptr);
        platform::ApplyScreenshotEdits(dc, region.right - region.left,
            region.bottom - region.top, edits_);
        if (saved) RestoreDC(dc, saved);
    }
    if (drag_mode_ == DragMode::draw_pixelate) {
        const RECT area{std::min(edit_start_.x, edit_current_.x),
            std::min(edit_start_.y, edit_current_.y),
            std::max(edit_start_.x, edit_current_.x),
            std::max(edit_start_.y, edit_current_.y)};
        DrawSelectionBorder(dc, ToClient(area, origin));
    }
    if (drag_mode_ == DragMode::erase_edits) {
        // 核心逻辑：拖动橡皮擦时显示实际命中半径；圆框只绘于预览，不进入输出图像。
        const int radius = std::max(4, platform::Scale(9, GetDpiForWindow(handle())));
        const POINT center{edit_current_.x - origin.x, edit_current_.y - origin.y};
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(24, 132, 255));
        if (pen) {
            HGDIOBJ old_pen = SelectObject(dc, pen);
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, center.x - radius, center.y - radius,
                center.x + radius, center.y + radius);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
    }

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
        const UINT dpi = GetDpiForWindow(handle());
        const RECT monitor = ToolbarMonitor(Selection()).bounds;
        const auto layout = MakeToolbarLayout(dpi,
            static_cast<int>(monitor.right - monitor.left));
        for (int index = 0; index < kToolbarButtonCount; ++index) {
            const int left = bar.left + layout.padding + index * (layout.button + layout.gap);
            const int top = bar.top + (layout.height - layout.button) / 2;
            RECT item{left, top, left + layout.button, top + layout.button};
            const ScreenshotAction action = kToolbarActions[index];
            const bool analysis_disabled = analysis_pending_ &&
                (action == ScreenshotAction::extract_text || action == ScreenshotAction::summarize ||
                 action == ScreenshotAction::translate);
            const bool history_disabled = (action == ScreenshotAction::undo && !history_.can_undo()) ||
                (action == ScreenshotAction::redo && !history_.can_redo());
            const bool disabled = analysis_disabled || history_disabled;
            const COLORREF background = disabled ? Color(77, 84, 86)
                : (action == ScreenshotAction::close ? Color(67, 76, 79)
                    : ((action == ScreenshotAction::pen && edit_tool_ == EditTool::pen) ||
                       (action == ScreenshotAction::pixelate && edit_tool_ == EditTool::pixelate) ||
                        (action == ScreenshotAction::eraser && edit_tool_ == EditTool::eraser) ||
                        (action == ScreenshotAction::add_text && edit_tool_ == EditTool::text)
                        ? Color(36, 108, 192) : Color(44, 135, 119)));
            Fill(dc, item, background);
            // 核心逻辑：按钮图标直接在截图后台表面绘制，保持透明背景与高 DPI 清晰度。
            const int icon_size = std::max(1, std::min(platform::Scale(23, dpi),
                layout.button - platform::Scale(8, dpi)));
            const int icon_left = (item.left + item.right - icon_size) / 2;
            const int icon_top = (item.top + item.bottom - icon_size) / 2;
            if (!DrawScreenshotToolbarIcon(dc, action,
                    {icon_left, icon_top, icon_left + icon_size, icon_top + icon_size},
                    disabled ? Color(190, 195, 197) : Color(255, 255, 255))) {
                Text(dc, L"?", item, platform::Scale(17, dpi), Color(255, 255, 255),
                    FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        if (toolbar_tooltip_visible_ && hovered_action_ != ScreenshotAction::none) {
            const auto found = std::find(kToolbarActions.begin(), kToolbarActions.end(), hovered_action_);
            if (found != kToolbarActions.end()) {
                const int index = static_cast<int>(found - kToolbarActions.begin());
                const int tooltip_width = platform::Scale(88, dpi);
                const int tooltip_height = platform::Scale(27, dpi);
                const int center = bar.left + layout.padding + index *
                    (layout.button + layout.gap) + layout.button / 2;
                const int left = std::clamp(center - tooltip_width / 2, 0,
                    std::max(0, frame_width_ - tooltip_width));
                const int top = bar.top >= tooltip_height + platform::Scale(5, dpi)
                    ? static_cast<int>(bar.top) - tooltip_height - platform::Scale(5, dpi)
                    : std::min(frame_height_ - tooltip_height,
                        static_cast<int>(bar.bottom) + platform::Scale(5, dpi));
                const RECT tooltip{left, top, left + tooltip_width, top + tooltip_height};
                Fill(dc, tooltip, Color(34, 43, 47));
                Text(dc, kToolbarNames[index], tooltip, platform::Scale(12, dpi),
                    Color(255, 255, 255), FW_SEMIBOLD,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
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
            if (text_editor_) FinishTextInput(true, false);
            ClearToolbarHover();
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const POINT screen_point = ScreenPoint(point);
            const RECT desktop = snapshot_ ? snapshot_->bounds() : RECT{};
            const RECT toolbar = ToClient(ToolbarBounds(), {desktop.left, desktop.top});
            if (selected_ && PtInRect(&toolbar, point)) {
                toolbar_pressed_ = true;
                SetCapture(handle());
                return 0;
            }

            drag_before_ = CaptureState();

            if (selected_ && edit_tool_ != EditTool::none &&
                HitResizeEdges(screen_point) == 0 && PtInRect(&selection_, screen_point)) {
                if (edit_tool_ == EditTool::text) {
                    BeginTextInput(screen_point);
                    return 0;
                }
                // 核心逻辑：编辑模式仅修改既有选区，不把笔迹手势误判为移动选区。
                edit_start_ = screen_point;
                edit_current_ = screen_point;
                drag_mode_ = edit_tool_ == EditTool::pen ? DragMode::draw_pen
                    : edit_tool_ == EditTool::pixelate ? DragMode::draw_pixelate
                    : DragMode::erase_edits;
                if (drag_mode_ == DragMode::draw_pen)
                    edits_.strokes.push_back({{{screen_point.x - selection_.left,
                        screen_point.y - selection_.top}}});
                else if (drag_mode_ == DragMode::erase_edits)
                    EraseBetween(screen_point, screen_point);
                result_panel_.Hide();
                SetCapture(handle());
                UpdateCursor(point);
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
                    edits_ = {};
                    edit_tool_ = EditTool::none;
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
            if (drag_mode_ != DragMode::none && analysis_result_visible_) {
                KillTimer(handle(), kResultPanelRestoreTimerId);
                result_panel_.Hide();
            }
            SetCapture(handle());
            UpdateCursor(point);
            InvalidateRect(handle(), nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (drag_mode_ == DragMode::draw_pen || drag_mode_ == DragMode::draw_pixelate ||
                drag_mode_ == DragMode::erase_edits) {
                const POINT raw = ScreenPoint({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                const POINT previous = edit_current_;
                edit_current_ = {std::clamp<LONG>(raw.x, selection_.left, selection_.right - 1),
                    std::clamp<LONG>(raw.y, selection_.top, selection_.bottom - 1)};
                if (drag_mode_ == DragMode::draw_pen && !edits_.strokes.empty())
                    edits_.strokes.back().points.push_back({edit_current_.x - selection_.left,
                        edit_current_.y - selection_.top});
                else if (drag_mode_ == DragMode::erase_edits)
                    EraseBetween(previous, edit_current_);
                InvalidateRect(handle(), nullptr, FALSE);
            } else if (drag_mode_ != DragMode::none) {
                UpdateDrag({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                InvalidateRect(handle(), nullptr, FALSE);
            }
            UpdateCursor({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            UpdateToolbarHover({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
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
            if (drag_mode_ == DragMode::draw_pen || drag_mode_ == DragMode::draw_pixelate ||
                drag_mode_ == DragMode::erase_edits) {
                const POINT raw = ScreenPoint({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                const POINT previous = edit_current_;
                edit_current_ = {std::clamp<LONG>(raw.x, selection_.left, selection_.right - 1),
                    std::clamp<LONG>(raw.y, selection_.top, selection_.bottom - 1)};
                if (drag_mode_ == DragMode::draw_pixelate) {
                    RECT area{std::min(edit_start_.x, edit_current_.x) - selection_.left,
                        std::min(edit_start_.y, edit_current_.y) - selection_.top,
                        std::max(edit_start_.x, edit_current_.x) - selection_.left + 1,
                        std::max(edit_start_.y, edit_current_.y) - selection_.top + 1};
                    if (area.right - area.left >= 2 && area.bottom - area.top >= 2)
                        edits_.pixelated_areas.push_back(area);
                } else if (drag_mode_ == DragMode::erase_edits) EraseBetween(previous, edit_current_);
                drag_mode_ = DragMode::none;
                ReleaseCapture();
                CommitState(drag_before_);
                InvalidateRect(handle(), nullptr, FALSE);
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
                    if (!selected_ && drag_before_.selected) RestoreState(drag_before_);
                }
                CommitState(drag_before_);
                UpdateCursor({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                InvalidateRect(handle(), nullptr, FALSE);
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (drag_mode_ != DragMode::none) RestoreState(drag_before_);
            drag_mode_ = DragMode::none;
            toolbar_pressed_ = false;
            resize_edges_ = 0;
            InvalidateRect(handle(), nullptr, FALSE);
            return 0;
        case WM_TIMER:
            if (wparam == kToolbarTooltipTimerId) {
                KillTimer(handle(), kToolbarTooltipTimerId);
                if (visible_ && selected_ && hovered_action_ != ScreenshotAction::none &&
                    drag_mode_ == DragMode::none) {
                    toolbar_tooltip_visible_ = true;
                    InvalidateRect(handle(), nullptr, FALSE);
                }
                return 0;
            }
            if (wparam == kResultPanelRestoreTimerId) {
                KillTimer(handle(), kResultPanelRestoreTimerId);
                if (visible_ && drag_mode_ == DragMode::none && analysis_result_visible_)
                    LayoutResultPanel();
                return 0;
            }
            return WindowBase::HandleMessage(message, wparam, lparam);
        case kFinishTextInputMessage:
            if (text_editor_ && GetFocus() != text_editor_) FinishTextInput(true, false);
            return 0;
        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam == 'Z') {
                Dispatch((GetKeyState(VK_SHIFT) & 0x8000)
                    ? ScreenshotAction::redo : ScreenshotAction::undo);
            } else if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam == 'Y') {
                Dispatch(ScreenshotAction::redo);
            } else if (wparam == VK_ESCAPE) Dispatch(ScreenshotAction::close);
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
