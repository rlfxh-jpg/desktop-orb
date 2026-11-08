// 文件职责：绘制和管理桌面围栏浏览窗口，提供分类筛选、滚动列表与 Shell 打开操作。
// Win32 子控件拥有自己的绘制和滚动；本窗口只协调布局、数据刷新及窗口生命周期。
#include "desktop_orb/ui/desktop_fence/fence_panel.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>

namespace desktop_orb::ui {
namespace {

constexpr UINT_PTR kRefreshTimerId = 3;
constexpr UINT kRefreshIntervalMs = 2500;
constexpr int kPanelWidth = 440;
constexpr int kPanelHeight = 560;
constexpr int kHeaderHeight = 72;
constexpr int kTabHeight = 34;
constexpr int kPanelPadding = 14;

// 作用：取得 Windows Shell 共享的小图标列表，供 ListView 显示文件类型图标。
// 参数：无。返回：借用的系统图像列表句柄；失败时为空，调用方不得销毁该句柄。
HIMAGELIST SystemSmallImageList() {
    SHFILEINFOW info{};
    const DWORD_PTR result = SHGetFileInfoW(L".", FILE_ATTRIBUTE_DIRECTORY, &info,
        sizeof(info), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    return result ? reinterpret_cast<HIMAGELIST>(result) : nullptr;
}

}  // 匿名命名空间结束

bool FencePanel::Create() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&controls) ||
        !CreateNative(L"DesktopOrbFencePanel", L"桌面围栏", WS_POPUP | WS_CAPTION,
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 0, 0, kPanelWidth, kPanelHeight)) {
        return false;
    }

    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_FOCUSNEVER,
        0, 0, 1, 1, handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 1, 1, handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
    empty_label_ = CreateWindowExW(0, L"STATIC", L"此分类没有桌面项目",
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 1, 1, handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!tabs_ || !list_ || !empty_label_) {
        DestroyWindow(handle());
        return false;
    }

    const WPARAM font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(tabs_, WM_SETFONT, font, TRUE);
    SendMessageW(list_, WM_SETFONT, font, TRUE);
    SendMessageW(empty_label_, WM_SETFONT, font, TRUE);
    constexpr platform::DesktopFenceCategory categories[] = {
        platform::DesktopFenceCategory::programs,
        platform::DesktopFenceCategory::folders,
        platform::DesktopFenceCategory::documents,
        platform::DesktopFenceCategory::other
    };
    for (int index = 0; index < static_cast<int>(std::size(categories)); ++index) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(platform::DesktopItemCatalog::CategoryName(categories[index]));
        TabCtrl_InsertItem(tabs_, index, &item);
    }
    TabCtrl_SetCurSel(tabs_, 0);

    system_icons_ = SystemSmallImageList();
    if (system_icons_) ListView_SetImageList(list_, system_icons_, LVSIL_SMALL);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                             LVS_EX_LABELTIP);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<wchar_t*>(L"名称");
    column.cx = 280;
    ListView_InsertColumn(list_, 0, &column);
    column.pszText = const_cast<wchar_t*>(L"类型");
    column.cx = 120;
    ListView_InsertColumn(list_, 1, &column);
    return true;
}

void FencePanel::ShowFor(const platform::Monitor& monitor, bool align_right) {
    if (visible_) {
        Hide();
        return;
    }
    monitor_ = monitor;
    align_right_ = align_right;
    Position();
    visible_ = true;
    ShowWindow(handle(), SW_SHOW);
    SetForegroundWindow(handle());
    Refresh();
    SetTimer(handle(), kRefreshTimerId, kRefreshIntervalMs, nullptr);
    SetFocus(list_);
}

void FencePanel::Hide() {
    if (!visible_) return;
    visible_ = false;
    KillTimer(handle(), kRefreshTimerId);
    items_.clear();
    ListView_DeleteAllItems(list_);
    ShowWindow(handle(), SW_HIDE);
    if (on_closed) on_closed();
}

void FencePanel::Position() {
    const UINT dpi = GetDpiForWindow(handle());
    const int work_width = monitor_.work.right - monitor_.work.left;
    const int work_height = monitor_.work.bottom - monitor_.work.top;
    const int width = std::min(work_width, platform::Scale(kPanelWidth, dpi));
    const int height = std::min(work_height, platform::Scale(kPanelHeight, dpi));
    const int x = align_right_ ? monitor_.work.right - width : monitor_.work.left;
    SetWindowPos(handle(), HWND_TOPMOST, x, monitor_.work.top + (work_height - height) / 2,
                 width, height, SWP_NOACTIVATE);
}

void FencePanel::Refresh() {
    if (!visible_) return;
    items_ = catalog_.List(SelectedCategory());
    ListView_DeleteAllItems(list_);
    for (size_t index = 0; index < items_.size(); ++index) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.iImage = items_[index].icon_index;
        item.lParam = static_cast<LPARAM>(index);
        item.pszText = items_[index].name.data();
        const int row = ListView_InsertItem(list_, &item);
        if (row >= 0) ListView_SetItemText(list_, row, 1, items_[index].type_name.data());
    }
    ShowWindow(empty_label_, items_.empty() ? SW_SHOW : SW_HIDE);
    InvalidateRect(handle(), nullptr, FALSE);
}

void FencePanel::LayoutControls(int width, int height, UINT dpi) {
    const int padding = platform::Scale(kPanelPadding, dpi);
    const int header = platform::Scale(kHeaderHeight, dpi);
    const int tab_height = platform::Scale(kTabHeight, dpi);
    const int tab_top = header - platform::Scale(6, dpi);
    const int content_top = tab_top + tab_height;
    if (tabs_) SetWindowPos(tabs_, nullptr, padding, tab_top, width - padding * 2,
        tab_height, SWP_NOZORDER | SWP_NOACTIVATE);
    if (list_) SetWindowPos(list_, nullptr, padding, content_top,
        width - padding * 2, height - content_top - padding,
        SWP_NOZORDER | SWP_NOACTIVATE);
    if (empty_label_) SetWindowPos(empty_label_, nullptr, padding, content_top,
        width - padding * 2, height - content_top - padding,
        SWP_NOZORDER | SWP_NOACTIVATE);
    if (list_) {
        ListView_SetColumnWidth(list_, 0, std::max(platform::Scale(150, dpi),
            width - padding * 2 - platform::Scale(130, dpi)));
        ListView_SetColumnWidth(list_, 1, platform::Scale(116, dpi));
    }
}

void FencePanel::OpenItem(size_t index) {
    if (index >= items_.size()) return;
    if (!platform::DesktopItemCatalog::Open(items_[index].path)) {
        MessageBoxW(handle(), L"Windows 无法打开这个桌面项目。", L"打开失败",
                    MB_OK | MB_ICONWARNING);
    }
}

platform::DesktopFenceCategory FencePanel::SelectedCategory() const {
    switch (TabCtrl_GetCurSel(tabs_)) {
        case 0: return platform::DesktopFenceCategory::programs;
        case 1: return platform::DesktopFenceCategory::folders;
        case 2: return platform::DesktopFenceCategory::documents;
        default: return platform::DesktopFenceCategory::other;
    }
}

LRESULT FencePanel::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(handle(), &paint);
            RECT client{};
            GetClientRect(handle(), &client);
            FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(38, 52, 57));
            RECT title{platform::Scale(kPanelPadding, GetDpiForWindow(handle())),
                platform::Scale(12, GetDpiForWindow(handle())), client.right, client.bottom};
            DrawTextW(dc, L"桌面围栏", -1, &title, DT_LEFT | DT_TOP | DT_SINGLELINE);
            EndPaint(handle(), &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            LayoutControls(LOWORD(lparam), HIWORD(lparam), GetDpiForWindow(handle()));
            return 0;
        case WM_DPICHANGED:
            Position();
            {
                RECT client{};
                GetClientRect(handle(), &client);
                LayoutControls(client.right - client.left, client.bottom - client.top,
                               HIWORD(wparam));
            }
            return 0;
        case WM_TIMER:
            if (wparam == kRefreshTimerId) {
                Refresh();
                return 0;
            }
            return WindowBase::HandleMessage(message, wparam, lparam);
        case WM_NOTIFY: {
            const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
            if (notification->hwndFrom == tabs_ && notification->code == TCN_SELCHANGE) {
                Refresh();
                return 0;
            }
            if (notification->hwndFrom == list_ && notification->code == NM_DBLCLK) {
                const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
                if (selected >= 0) OpenItem(static_cast<size_t>(selected));
                return 0;
            }
            if (notification->hwndFrom == list_ && notification->code == LVN_KEYDOWN) {
                const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(lparam);
                if (key->wVKey == VK_ESCAPE) Hide();
                else if (key->wVKey == VK_RETURN) {
                    const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
                    if (selected >= 0) OpenItem(static_cast<size_t>(selected));
                }
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) Hide();
            else if (wparam == VK_RETURN) {
                const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
                if (selected >= 0) OpenItem(static_cast<size_t>(selected));
            }
            return 0;
        case WM_CLOSE:
            Hide();
            return 0;
        default:
            break;
    }
    return WindowBase::HandleMessage(message, wparam, lparam);
}

}  // desktop_orb::ui 命名空间结束
