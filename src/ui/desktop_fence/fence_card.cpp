// 文件职责：实现单张围栏卡片的窗口消息、项目交互、滚动和资源生命周期。
// 卡片通过 FenceCard 接口供管理器使用；窗口绘制和 Windows Shell 资源分别委托专属模块。
#include "fence_card.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include "desktop_orb/platform/desktop_fence/desktop_wallpaper.hpp"
#include "desktop_orb/ui/common/window_base.hpp"
#include "fence_card_renderer.hpp"
#include <algorithm>
#include <commctrl.h>
#include <cstdlib>
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>

namespace desktop_orb::ui {
namespace {

constexpr UINT_PTR kRefreshTimerId = 3;
constexpr UINT kRefreshIntervalMs = 2500;
constexpr int kCardWidth = 390;
constexpr int kCardHeight = 310;
constexpr int kCardHeaderHeight = 64;
constexpr int kCardPadding = 16;
constexpr int kGridCellWidth = 80;
constexpr int kGridCellHeight = 96;

}  // 匿名命名空间结束
class FenceCardWindow final : public WindowBase, public FenceCard {
public:
    // 作用：释放卡片缓存的 Shell 项目图标。
    // 参数：无。返回：无。
    ~FenceCardWindow() override {
        if (category_icon_) DestroyIcon(category_icon_);
        ReleaseItemIcons();
    }

    // 作用：创建指定类别围栏卡片并初始化 Shell 图标网格。
    // 参数：category 为卡片类别。返回：所有原生控件创建成功为 true，否则为 false。
    bool Create(platform::DesktopFenceCategory category) {
        category_ = category;
        if (!CreateNative(L"DesktopOrbFenceCard", L"桌面围栏", WS_POPUP,
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                0, 0, kCardWidth, kCardHeight)) return false;
        // 核心逻辑：围栏关闭 DWM 系统材质，使用壁纸像素作为底图；窗口本身保持不透明，
        // 防止编辑器或其他前景窗口的文字透过玻璃面板。
        category_icon_ = platform::DesktopItemCatalog::LoadCategoryIcon(category_);
        return true;
    }

    // 作用：显示卡片并刷新对应桌面分类。
    // 参数：bounds 为屏幕坐标卡片矩形。返回：窗口成功定位并显示为 true，否则为 false。
    bool ShowAt(RECT bounds, RECT monitor_bounds) {
        if (!handle() || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return false;
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        if (!SetWindowPos(handle(), HWND_TOPMOST, bounds.left, bounds.top, width, height,
                          SWP_NOACTIVATE | SWP_SHOWWINDOW)) return false;
        // 核心逻辑：成功设置后区域句柄由系统接管；失败时必须由当前对象释放。
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
            platform::Scale(22, GetDpiForWindow(handle())),
            platform::Scale(22, GetDpiForWindow(handle())));
        if (region && !SetWindowRgn(handle(), region, TRUE)) DeleteObject(region);
        (void)width;
        (void)height;
        visible_ = true;
        loaded_ = false;
        monitor_bounds_ = monitor_bounds;
        LoadWallpaper();
        scroll_row_ = 0;
        Refresh();
        SetTimer(handle(), kRefreshTimerId, kRefreshIntervalMs, nullptr);
        return true;
    }

    // 作用：隐藏卡片、停止刷新并清空当前项目。参数：无。返回：无。
    void Hide() {
        if (!visible_) return;
        visible_ = false;
        KillTimer(handle(), kRefreshTimerId);
        items_.clear();
        ReleaseItemIcons();
        loaded_ = false;
        ShowWindow(handle(), SW_HIDE);
    }

    // 作用：取得卡片当前屏幕矩形。
    // 参数：无。返回：屏幕矩形；窗口未创建或系统读取失败时返回零矩形。
    [[nodiscard]] RECT Bounds() const {
        RECT bounds{};
        if (handle()) GetWindowRect(handle(), &bounds);
        return bounds;
    }

    // 作用：查询卡片是否可见。参数：无。返回：显示为 true，否则为 false。
    [[nodiscard]] bool visible() const override { return visible_; }
    // 作用：向围栏管理器暴露当前窗口句柄。参数：无。返回：当前卡片 HWND。
    [[nodiscard]] HWND handle() const override { return WindowBase::handle(); }

protected:
    // 作用：处理标题绘制、图标激活、刷新和标题栏拖动。
    // 参数：message 为消息码；wparam、lparam 为消息数据。返回：处理结果或默认窗口过程结果。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override {
        switch (message) {
            case WM_PAINT: {
                PAINTSTRUCT paint{};
                HDC dc = BeginPaint(handle(), &paint);
                RECT client{};
                GetClientRect(handle(), &client);
                PaintCard(dc, client);
                EndPaint(handle(), &paint);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_MOUSEWHEEL: {
                const int direction = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1;
                ScrollRows(direction * std::max(1, std::abs(GET_WHEEL_DELTA_WPARAM(wparam)) /
                                                   WHEEL_DELTA));
                return 0;
            }
            case WM_LBUTTONDOWN: {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (HitScrollbar(point)) {
                    scrollbar_dragging_ = true;
                    SetCapture(handle());
                    ScrollToPoint(point.y);
                    return 0;
                }
                selected_item_ = HitItem(point);
                SetFocus(handle());
                InvalidateRect(handle(), nullptr, FALSE);
                return 0;
            }
            case WM_LBUTTONDBLCLK: {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                selected_item_ = HitItem(point);
                if (selected_item_) OpenItem(*selected_item_);
                return 0;
            }
            case WM_LBUTTONUP:
                if (scrollbar_dragging_) {
                    scrollbar_dragging_ = false;
                    ReleaseCapture();
                }
                return 0;
            case WM_MOUSEMOVE: {
                if (scrollbar_dragging_) {
                    ScrollToPoint(GET_Y_LPARAM(lparam));
                    return 0;
                }
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                const auto next_hover = HitItem(point);
                if (next_hover != hovered_item_) {
                    hovered_item_ = next_hover;
                    InvalidateRect(handle(), nullptr, FALSE);
                }
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, handle(), 0};
                TrackMouseEvent(&tracking);
                return 0;
            }
            case WM_MOUSELEAVE:
                hovered_item_.reset();
                InvalidateRect(handle(), nullptr, FALSE);
                return 0;
            case WM_CAPTURECHANGED:
                scrollbar_dragging_ = false;
                return 0;
            case WM_TIMER:
                if (wparam == kRefreshTimerId) {
                    Refresh();
                    return 0;
                }
                break;
            case WM_KEYDOWN:
                if (wparam == VK_RETURN && selected_item_) OpenItem(*selected_item_);
                else if (wparam == VK_UP) ScrollRows(-1);
                else if (wparam == VK_DOWN) ScrollRows(1);
                return 0;
            case WM_NCHITTEST: {
                const LRESULT hit = DefWindowProcW(handle(), message, wparam, lparam);
                if (hit != HTCLIENT) return hit;
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(handle(), &point);
                if (point.y < platform::Scale(kCardHeaderHeight, GetDpiForWindow(handle())))
                    return HTCAPTION;
                return HTCLIENT;
            }
            default:
                break;
        }
        return WindowBase::HandleMessage(message, wparam, lparam);
    }

private:
    // 作用：计算当前宽度可以容纳的图标列数。
    // 参数：无。返回：至少为一列的网格列数。
    int ColumnCount() const {
        RECT client{};
        GetClientRect(handle(), &client);
        const UINT dpi = GetDpiForWindow(handle());
        const int cell_width = platform::Scale(kGridCellWidth, dpi);
        const int available = std::max(cell_width,
            static_cast<int>(client.right) - platform::Scale(kCardPadding * 2 + 12, dpi));
        return std::max(1, available / cell_width);
    }

    // 作用：计算内容区域可见的整行图标数和总行数。
    // 参数：visible_rows 用于接收视口可见行数。返回：当前类别的总行数。
    int TotalRows(int& visible_rows) const {
        RECT client{};
        GetClientRect(handle(), &client);
        const UINT dpi = GetDpiForWindow(handle());
        const int cell_height = platform::Scale(kGridCellHeight, dpi);
        const int content_top = platform::Scale(kCardHeaderHeight + 16, dpi);
        const int content_height = std::max(0, static_cast<int>(client.bottom) - content_top -
                                               platform::Scale(kCardPadding, dpi));
        visible_rows = std::max(1, content_height / cell_height);
        const int columns = ColumnCount();
        return static_cast<int>((items_.size() + static_cast<size_t>(columns) - 1) /
                                static_cast<size_t>(columns));
    }

    // 作用：把鼠标坐标映射到当前滚动位置对应的项目。
    // 参数：point 为窗口客户区坐标。返回：命中项目的索引；空白处或标题区返回空值。
    std::optional<size_t> HitItem(POINT point) const {
        RECT client{};
        GetClientRect(handle(), &client);
        const UINT dpi = GetDpiForWindow(handle());
        const int padding = platform::Scale(kCardPadding, dpi);
        const int content_top = platform::Scale(kCardHeaderHeight + 16, dpi);
        const int cell_width = platform::Scale(kGridCellWidth, dpi);
        const int cell_height = platform::Scale(kGridCellHeight, dpi);
        if (point.x < padding || point.x >= client.right - padding || point.y < content_top)
            return std::nullopt;
        const size_t row = static_cast<size_t>((point.y - content_top) / cell_height) +
                           static_cast<size_t>(scroll_row_);
        const size_t row_start = row * static_cast<size_t>(ColumnCount());
        if (row_start >= items_.size()) return std::nullopt;
        const size_t row_count = std::min(static_cast<size_t>(ColumnCount()), items_.size() - row_start);
        const int available = static_cast<int>(client.right) - padding * 2 - platform::Scale(12, dpi);
        const int row_left = padding + std::max(0,
            (available - static_cast<int>(row_count) * cell_width) / 2);
        if (point.x < row_left) return std::nullopt;
        const size_t column = static_cast<size_t>((point.x - row_left) / cell_width);
        if (column >= row_count) return std::nullopt;
        const size_t index = row_start + column;
        return index < items_.size() ? std::optional<size_t>(index) : std::nullopt;
    }

    // 作用：判断鼠标是否命中需要滚动的右侧覆盖式滚动条。
    // 参数：point 为窗口客户区坐标。返回：内容溢出且命中滚动条轨道时为 true。
    bool HitScrollbar(POINT point) const {
        int visible_rows = 0;
        const int total_rows = TotalRows(visible_rows);
        if (total_rows <= visible_rows) return false;
        RECT client{};
        GetClientRect(handle(), &client);
        const UINT dpi = GetDpiForWindow(handle());
        const int track_x = client.right - platform::Scale(kCardPadding, dpi);
        const int track_top = platform::Scale(kCardHeaderHeight + 16, dpi);
        const int track_bottom = client.bottom - platform::Scale(kCardPadding, dpi);
        return point.x >= track_x - platform::Scale(8, dpi) && point.x <= track_x +
            platform::Scale(4, dpi) && point.y >= track_top && point.y <= track_bottom;
    }

    // 作用：按行滚动并限制在项目网格的有效范围内。
    // 参数：delta 为滚动行数，可为正向下或负向上。返回：无。
    void ScrollRows(int delta) {
        int visible_rows = 0;
        const int total_rows = TotalRows(visible_rows);
        const int maximum = std::max(0, total_rows - visible_rows);
        const int next = std::clamp(scroll_row_ + delta, 0, maximum);
        if (next == scroll_row_) return;
        scroll_row_ = next;
        hovered_item_.reset();
        InvalidateRect(handle(), nullptr, FALSE);
    }

    // 作用：根据滚动条轨道内的鼠标纵坐标定位首个可见行。
    // 参数：y 为窗口客户区纵坐标。返回：无；没有溢出内容时不改变滚动位置。
    void ScrollToPoint(int y) {
        int visible_rows = 0;
        const int total_rows = TotalRows(visible_rows);
        const int maximum = std::max(0, total_rows - visible_rows);
        if (maximum == 0) return;
        RECT client{};
        GetClientRect(handle(), &client);
        const UINT dpi = GetDpiForWindow(handle());
        const int top = platform::Scale(kCardHeaderHeight + 16, dpi);
        const int bottom = client.bottom - platform::Scale(kCardPadding, dpi);
        const int track_height = std::max(1, bottom - top);
        const int next = std::clamp((y - top) * maximum / track_height, 0, maximum);
        if (next == scroll_row_) return;
        scroll_row_ = next;
        InvalidateRect(handle(), nullptr, FALSE);
    }

    // 作用：绘制围栏表面、分类标题、图标网格和覆盖式滚动条。
    // 参数：dc 为窗口绘图设备上下文；client 为客户区矩形。返回：无。
    void PaintCard(HDC dc, const RECT& client) {
        const UINT dpi = GetDpiForWindow(handle());
        DrawFenceBackdrop(dc, handle(), client, dpi, monitor_bounds_, wallpaper_.get());
        DrawFenceContents(dc, client, dpi, category_, category_icon_, items_, item_icons_,
                          selected_item_, hovered_item_, scroll_row_);
    }

    // 作用：读取当前类别项目，仅在内容有变化时重建图标网格。
    // 参数：无。返回：无；目录短暂不可访问时显示空状态。
    void Refresh() {
        auto next_items = catalog_.List(category_);
        const bool unchanged = next_items.size() == items_.size() &&
            std::equal(next_items.begin(), next_items.end(), items_.begin(),
                [](const platform::DesktopItem& left, const platform::DesktopItem& right) {
                    return left.path == right.path && left.name == right.name;
                });
        if (loaded_ && unchanged) return;
        loaded_ = true;
        items_ = std::move(next_items);
        ReleaseItemIcons();
        item_icons_.reserve(items_.size());
        // 核心逻辑：直接按完整路径向 Shell 查询项目图标，支持快捷方式自定义图标和文件类型图标；
        // 每个 HICON 都归当前卡片所有，避免依赖系统共享列表的索引尺寸或索引来源。
        for (const auto& item : items_)
            item_icons_.push_back(platform::DesktopItemCatalog::LoadIcon(item.path));
        int visible_rows = 0;
        scroll_row_ = std::min(scroll_row_, std::max(0, TotalRows(visible_rows) - visible_rows));
        if (selected_item_ && *selected_item_ >= items_.size()) selected_item_.reset();
        InvalidateRect(handle(), nullptr, FALSE);
    }

    // 作用：释放当前卡片缓存的所有 Shell 项目图标句柄。
    // 参数：无。返回：无；空句柄会被跳过，释放后缓存为空。
    void ReleaseItemIcons() {
        for (HICON icon : item_icons_) if (icon) DestroyIcon(icon);
        item_icons_.clear();
    }

    // 作用：重新读取显示器壁纸并解码到卡片本地缓存。
    // 参数：无。返回：无；GDI+、COM 或壁纸解码失败时保留纯色回退背景。
    void LoadWallpaper() {
        wallpaper_.reset();
        if (!InitializeFenceRenderer()) return;
        const std::wstring path = platform::DesktopWallpaper::PathForMonitor(monitor_bounds_);
        if (path.empty()) return;
        auto image = std::make_unique<Gdiplus::Bitmap>(path.c_str());
        if (image->GetLastStatus() == Gdiplus::Ok) wallpaper_ = std::move(image);
    }

    // 作用：通过 Windows Shell 打开指定的围栏项目。
    // 参数：index 为当前图标网格的项目索引。返回：无；Shell 打开失败时保留窗口并提示。
    void OpenItem(size_t index) {
        if (index >= items_.size()) return;
        if (!platform::DesktopItemCatalog::Open(items_[index].path)) {
            MessageBoxW(handle(), L"Windows 无法打开这个桌面项目。", L"打开失败",
                        MB_OK | MB_ICONWARNING);
        }
    }

    platform::DesktopItemCatalog catalog_;
    platform::DesktopFenceCategory category_ = platform::DesktopFenceCategory::other;
    HICON category_icon_ = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> wallpaper_;
    RECT monitor_bounds_{};
    std::vector<HICON> item_icons_;
    std::vector<platform::DesktopItem> items_;
    std::optional<size_t> selected_item_;
    std::optional<size_t> hovered_item_;
    int scroll_row_ = 0;
    bool scrollbar_dragging_ = false;
    bool visible_ = false;
    bool loaded_ = false;
};
// 作用：创建围栏单卡片的私有 Win32 实现。
// 参数：无。返回：拥有窗口及其图标资源的卡片对象。
std::unique_ptr<FenceCard> MakeFenceCard() {
    return std::make_unique<FenceCardWindow>();
}

}  // desktop_orb::ui 命名空间结束