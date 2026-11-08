// 文件职责：实现四张可独立移动的半透明桌面围栏卡片及其图标网格。
// 每张卡片拥有列表控件和刷新计时器；目录枚举、分类和 Shell 打开由平台层负责。
#include "desktop_orb/ui/desktop_fence/fence_panel.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include "desktop_orb/platform/desktop_fence/desktop_item_catalog.hpp"
#include "desktop_orb/ui/common/window_base.hpp"
#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

namespace desktop_orb::ui {
namespace {

constexpr UINT_PTR kRefreshTimerId = 3;
constexpr UINT kRefreshIntervalMs = 2500;
constexpr int kCardWidth = 390;
constexpr int kCardHeight = 310;
constexpr int kCardGap = 18;
constexpr int kCardHeaderHeight = 48;
constexpr int kCardPadding = 16;
constexpr COLORREF kTextColor = RGB(35, 43, 57);
constexpr COLORREF kCardSurface = RGB(238, 241, 248);
constexpr COLORREF kCardBorder = RGB(255, 255, 255);
constexpr platform::DesktopFenceCategory kCategories[] = {
    platform::DesktopFenceCategory::programs,
    platform::DesktopFenceCategory::folders,
    platform::DesktopFenceCategory::documents,
    platform::DesktopFenceCategory::other
};

// 作用：取得 Windows Shell 的共享大图标列表供围栏项目网格使用。
// 参数：无。返回：借用的系统图像列表；获取失败时为空，调用方不释放该句柄。
HIMAGELIST SystemLargeImageList() {
    SHFILEINFOW info{};
    const DWORD_PTR result = SHGetFileInfoW(L".", FILE_ATTRIBUTE_DIRECTORY, &info,
        sizeof(info), SHGFI_SYSICONINDEX | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
    return result ? reinterpret_cast<HIMAGELIST>(result) : nullptr;
}

// 作用：取得与项目类别相符的标题图标。
// 参数：category 为桌面围栏类别。返回：新建的 HICON；获取失败时为空，成功句柄由调用方销毁。
HICON CategoryIcon(platform::DesktopFenceCategory category) {
    const wchar_t* path = L".";
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    if (category == platform::DesktopFenceCategory::folders) {
        attributes = FILE_ATTRIBUTE_DIRECTORY;
    } else if (category == platform::DesktopFenceCategory::programs) {
        path = L".lnk";
    } else if (category == platform::DesktopFenceCategory::documents) {
        path = L".txt";
    } else {
        path = L".dat";
    }
    SHFILEINFOW info{};
    if (!SHGetFileInfoW(path, attributes, &info, sizeof(info),
                        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES)) return nullptr;
    return info.hIcon;
}

}  // 匿名命名空间结束

class FencePanel::Card final : public WindowBase {
public:
    // 作用：释放标题图标；系统共享图像列表由 Windows Shell 管理。
    // 参数：无。返回：无。
    ~Card() override {
        if (category_icon_) DestroyIcon(category_icon_);
    }

    // 作用：创建指定类别围栏卡片并初始化 Shell 图标网格。
    // 参数：category 为卡片类别。返回：所有原生控件创建成功为 true，否则为 false。
    bool Create(platform::DesktopFenceCategory category) {
        category_ = category;
        if (!CreateNative(L"DesktopOrbFenceCard", L"桌面围栏", WS_POPUP,
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                0, 0, kCardWidth, kCardHeight)) return false;

        // 核心逻辑：交由 DWM 绘制系统圆角、阴影与 Windows 11 半透明材质，
        // 遇到旧版系统时这些属性会被忽略，卡片仍使用下方的纯色底面显示。
        constexpr DWORD kCornerPreferenceAttribute = 33;
        constexpr DWORD kBorderColorAttribute = 34;
        constexpr DWORD kSystemBackdropAttribute = 38;
        constexpr int kRoundCorners = 2;
        constexpr int kTransientBackdrop = 3;
        const COLORREF border_color = RGB(255, 255, 255);
        DwmSetWindowAttribute(handle(), kCornerPreferenceAttribute,
                              &kRoundCorners, sizeof(kRoundCorners));
        DwmSetWindowAttribute(handle(), kBorderColorAttribute,
                              &border_color, sizeof(border_color));
        glass_enabled_ = SUCCEEDED(DwmSetWindowAttribute(handle(), kSystemBackdropAttribute,
            &kTransientBackdrop, sizeof(kTransientBackdrop)));
        if (glass_enabled_) {
            const MARGINS glass_margins{-1, -1, -1, -1};
            DwmExtendFrameIntoClientArea(handle(), &glass_margins);
        }

        list_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_AUTOARRANGE |
                LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOLABELWRAP,
            0, 0, 1, 1, handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
        empty_label_ = CreateWindowExW(0, L"STATIC", L"这里还没有项目",
            WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
            0, 0, 1, 1, handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!list_ || !empty_label_) return false;

        const WPARAM font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(list_, WM_SETFONT, font, TRUE);
        SendMessageW(empty_label_, WM_SETFONT, font, TRUE);
        image_list_ = SystemLargeImageList();
        if (image_list_) ListView_SetImageList(list_, image_list_, LVSIL_NORMAL);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        ListView_SetBkColor(list_, kCardSurface);
        ListView_SetTextBkColor(list_, CLR_NONE);
        ListView_SetTextColor(list_, kTextColor);
        category_icon_ = CategoryIcon(category_);
        const UINT dpi = GetDpiForWindow(handle());
        ListView_SetIconSpacing(list_, platform::Scale(86, dpi), platform::Scale(96, dpi));
        return true;
    }

    // 作用：显示卡片并刷新对应桌面分类。
    // 参数：bounds 为屏幕坐标卡片矩形。返回：窗口成功定位并显示为 true，否则为 false。
    bool ShowAt(RECT bounds) {
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
        LayoutControls(width, height, GetDpiForWindow(handle()));
        visible_ = true;
        loaded_ = false;
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
        loaded_ = false;
        ListView_DeleteAllItems(list_);
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
    [[nodiscard]] bool visible() const { return visible_; }

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
                if (!glass_enabled_) {
                    HBRUSH fill = CreateSolidBrush(kCardSurface);
                    FillRect(dc, &client, fill);
                    DeleteObject(fill);
                }
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, kTextColor);
                const UINT dpi = GetDpiForWindow(handle());
                const int padding = platform::Scale(kCardPadding, dpi);
                if (category_icon_) DrawIconEx(dc, padding, platform::Scale(14, dpi),
                    category_icon_, platform::Scale(18, dpi), platform::Scale(18, dpi),
                    0, nullptr, DI_NORMAL);
                HFONT title_font = CreateFontW(platform::Scale(15, dpi), 0, 0, 0,
                    FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HGDIOBJ old_font = SelectObject(dc, title_font);
                RECT title{padding + platform::Scale(26, dpi), 0,
                    client.right - padding, platform::Scale(kCardHeaderHeight, dpi)};
                const std::wstring heading =
                    std::wstring(platform::DesktopItemCatalog::CategoryName(category_)) +
                    L"  " + std::to_wstring(items_.size());
                DrawTextW(dc, heading.c_str(), -1, &title,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(dc, old_font);
                DeleteObject(title_font);
                HPEN divider = CreatePen(PS_SOLID, 1, kCardBorder);
                HGDIOBJ old_pen = SelectObject(dc, divider);
                MoveToEx(dc, padding, platform::Scale(kCardHeaderHeight, dpi), nullptr);
                LineTo(dc, client.right - padding, platform::Scale(kCardHeaderHeight, dpi));
                SelectObject(dc, old_pen);
                DeleteObject(divider);
                HPEN outline = CreatePen(PS_SOLID, 1, kCardBorder);
                HGDIOBJ old_outline = SelectObject(dc, outline);
                HGDIOBJ old_fill = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(dc, 0, 0, client.right, client.bottom);
                SelectObject(dc, old_fill);
                SelectObject(dc, old_outline);
                DeleteObject(outline);
                EndPaint(handle(), &paint);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
                LayoutControls(LOWORD(lparam), HIWORD(lparam), GetDpiForWindow(handle()));
                return 0;
            case WM_DPICHANGED: {
                RECT client{};
                GetClientRect(handle(), &client);
                LayoutControls(client.right - client.left, client.bottom - client.top,
                               HIWORD(wparam));
                return 0;
            }
            case WM_TIMER:
                if (wparam == kRefreshTimerId) {
                    Refresh();
                    return 0;
                }
                break;
            case WM_NCHITTEST: {
                const LRESULT hit = DefWindowProcW(handle(), message, wparam, lparam);
                if (hit != HTCLIENT) return hit;
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(handle(), &point);
                if (point.y < platform::Scale(kCardHeaderHeight, GetDpiForWindow(handle())))
                    return HTCAPTION;
                return HTCLIENT;
            }
            case WM_NOTIFY: {
                const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
                if (notification->hwndFrom == list_ && notification->code == NM_DBLCLK) {
                    const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
                    if (selected >= 0) OpenItem(static_cast<size_t>(selected));
                    return 0;
                }
                if (notification->hwndFrom == list_ && notification->code == LVN_KEYDOWN) {
                    const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(lparam);
                    if (key->wVKey == VK_RETURN) {
                        const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
                        if (selected >= 0) OpenItem(static_cast<size_t>(selected));
                    }
                    return 0;
                }
                break;
            }
            default:
                break;
        }
        return WindowBase::HandleMessage(message, wparam, lparam);
    }

private:
    // 作用：把图标网格和空状态提示放在标题分隔线下方。
    // 参数：width、height 为窗口客户区尺寸；dpi 为卡片所在显示器缩放值。返回：无。
    void LayoutControls(int width, int height, UINT dpi) {
        const int padding = platform::Scale(kCardPadding, dpi);
        const int top = platform::Scale(kCardHeaderHeight, dpi);
        if (list_) SetWindowPos(list_, nullptr, padding, top + platform::Scale(4, dpi),
            std::max(0, width - padding * 2), std::max(0, height - top - padding),
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (empty_label_) SetWindowPos(empty_label_, nullptr, padding, top + platform::Scale(4, dpi),
            std::max(0, width - padding * 2), std::max(0, height - top - padding),
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (list_) ListView_SetIconSpacing(list_, platform::Scale(92, dpi),
                                          platform::Scale(104, dpi));
    }

    // 作用：读取当前类别项目，仅在内容有变化时重建图标网格。
    // 参数：无。返回：无；目录短暂不可访问时显示空状态。
    void Refresh() {
        auto next_items = catalog_.List(category_);
        const bool unchanged = next_items.size() == items_.size() &&
            std::equal(next_items.begin(), next_items.end(), items_.begin(),
                [](const platform::DesktopItem& left, const platform::DesktopItem& right) {
                    return left.path == right.path && left.name == right.name &&
                        left.icon_index == right.icon_index;
                });
        if (loaded_ && unchanged) return;
        loaded_ = true;
        items_ = std::move(next_items);
        ListView_DeleteAllItems(list_);
        for (size_t index = 0; index < items_.size(); ++index) {
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            item.iItem = static_cast<int>(index);
            item.iImage = items_[index].icon_index;
            item.lParam = static_cast<LPARAM>(index);
            item.pszText = items_[index].name.data();
            ListView_InsertItem(list_, &item);
        }
        ShowWindow(empty_label_, items_.empty() ? SW_SHOW : SW_HIDE);
        InvalidateRect(handle(), nullptr, FALSE);
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
    HWND list_ = nullptr;
    HWND empty_label_ = nullptr;
    HIMAGELIST image_list_ = nullptr;
    HICON category_icon_ = nullptr;
    std::vector<platform::DesktopItem> items_;
    bool glass_enabled_ = false;
    bool visible_ = false;
    bool loaded_ = false;
};

// 作用：销毁四个卡片对象及各自持有的窗口。参数：无。返回：无。
FencePanel::FencePanel() = default;
FencePanel::~FencePanel() = default;

// 作用：初始化列表视图公共控件并创建四张分类卡片。
// 参数：无。返回：全部卡片成功创建为 true；任一失败则释放已创建卡片并返回 false。
bool FencePanel::Create() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&controls)) return false;
    for (size_t index = 0; index < cards_.size(); ++index) {
        cards_[index] = std::make_unique<Card>();
        if (!cards_[index]->Create(kCategories[index])) {
            cards_ = {};
            return false;
        }
    }
    return true;
}

// 作用：在指定显示器工作区内展示四张围栏卡片。
// 参数：monitor 为悬浮球所在显示器；align_right 为卡片组靠右排列标志。
// 返回：无；若卡片组已显示则改为隐藏，若空间不足则按工作区缩小卡片。
void FencePanel::ShowFor(const platform::Monitor& monitor, bool align_right) {
    if (visible_) {
        Hide();
        return;
    }
    const int work_width = monitor.work.right - monitor.work.left;
    const int work_height = monitor.work.bottom - monitor.work.top;
    const UINT dpi = monitor.dpi ? monitor.dpi : 96;
    const int gap = std::min(platform::Scale(kCardGap, dpi),
        std::max(1, std::min(work_width, work_height) / 12));
    const int card_width = std::min(platform::Scale(kCardWidth, dpi),
                                    std::max(1, (work_width - gap * 3) / 2));
    const int card_height = std::min(platform::Scale(kCardHeight, dpi),
                                     std::max(1, (work_height - gap * 3) / 2));
    const int group_width = card_width * 2 + gap;
    const int group_height = card_height * 2 + gap;
    const int group_left = align_right ? monitor.work.right - group_width - gap
                                       : monitor.work.left + gap;
    const int group_top = monitor.work.top + std::max(0, (work_height - group_height) / 2);
    bool all_cards_shown = true;
    for (size_t index = 0; index < cards_.size(); ++index) {
        const int column = static_cast<int>(index % 2);
        const int row = static_cast<int>(index / 2);
        const int left = group_left + column * (card_width + gap);
        const int top = group_top + row * (card_height + gap);
        all_cards_shown = cards_[index]->ShowAt(
            {left, top, left + card_width, top + card_height}) && all_cards_shown;
    }
    if (!all_cards_shown) {
        for (const auto& card : cards_) {
            if (card) card->Hide();
        }
        return;
    }
    visible_ = true;
    if (cards_[0]) SetForegroundWindow(cards_[0]->handle());
}

// 作用：隐藏卡片并通知应用层恢复悬浮球。
// 参数：无。返回：无；卡片原本隐藏时不重复通知。
void FencePanel::Hide() {
    if (!visible_) return;
    visible_ = false;
    for (const auto& card : cards_) {
        if (card) card->Hide();
    }
    if (on_closed) on_closed();
}

// 作用：收集已创建卡片的窗口句柄。
// 参数：无。返回：非空的卡片 HWND 列表，未创建的卡片会被跳过。
std::vector<HWND> FencePanel::Handles() const {
    std::vector<HWND> handles;
    handles.reserve(cards_.size());
    for (const auto& card : cards_) {
        if (card && card->handle()) handles.push_back(card->handle());
    }
    return handles;
}

// 作用：判断屏幕坐标是否位于当前可见卡片内。
// 参数：point 为屏幕坐标。返回：命中任意可见卡片为 true，否则为 false。
bool FencePanel::Contains(POINT point) const {
    for (const auto& card : cards_) {
        if (!card || !card->visible()) continue;
        const RECT bounds = card->Bounds();
        if (point.x >= bounds.left && point.x < bounds.right &&
            point.y >= bounds.top && point.y < bounds.bottom) return true;
    }
    return false;
}

}  // desktop_orb::ui 命名空间结束
