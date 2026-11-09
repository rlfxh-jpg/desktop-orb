// 文件职责：呈现可搜索的 Windows 应用侧边列表，并把单击操作交给 Shell 启动。
// 顶层 HWND 由 WindowBase 持有；子控件由 Windows 随父窗口销毁，字体和图标由本对象释放。
#include "desktop_orb/ui/app_launcher/app_list_panel.hpp"

#include "desktop_orb/ui/common/drawing.hpp"
#include <algorithm>
#include <cwctype>
#include <commctrl.h>

namespace desktop_orb::ui {
namespace {

constexpr int kPanelWidth = 420;
constexpr int kHeaderHeight = 116;
constexpr int kPadding = 18;
constexpr int kSearchTop = 62;
constexpr int kSearchHeight = 36;
constexpr int kRowHeight = 52;
constexpr size_t kDesktopHeader = static_cast<size_t>(-1);
constexpr size_t kPackagedHeader = static_cast<size_t>(-2);
constexpr UINT kIconReady = WM_APP + 17;
constexpr UINT kAppClicked = WM_APP + 18;
constexpr UINT_PTR kListSubclassId = 1;
constexpr UINT_PTR kScrollSettledTimer = 2;
constexpr UINT kScrollSettleMs = 120;

// 作用：判断列表行是否为分组标题。参数：index 为列表映射索引。返回：标题行时为 true。
bool IsHeader(size_t index) { return index == kDesktopHeader || index == kPackagedHeader; }

// 作用：执行不区分大小写的应用名称子串搜索。
// 参数：name 为应用名称；query 为输入词。返回：名称包含输入词时为 true。
bool Matches(const std::wstring& name, const std::wstring& query) {
    if (query.empty()) return true;
    return std::search(name.begin(), name.end(), query.begin(), query.end(),
        [](wchar_t left, wchar_t right) { return std::towlower(left) == std::towlower(right); })
        != name.end();
}

}  // 匿名命名空间结束

// 作用：先销毁引用自定义字体的子控件，再释放 GDI 字体和 Shell 图标。
// 参数：无。返回：无；顶层 HWND 仍由 WindowBase 析构。
AppListPanel::~AppListPanel() {
    icon_worker_.request_stop();
    if (icon_worker_.joinable()) icon_worker_.join();
    if (search_ && IsWindow(search_)) DestroyWindow(search_);
    if (list_ && IsWindow(list_)) {
        RemoveWindowSubclass(list_, ListSubclass, kListSubclassId);
        DestroyWindow(list_);
    }
    ReleaseIcons();
    if (font_) DeleteObject(font_);
}

// 作用：创建应用面板、搜索框和原生可滚动列表。
// 参数：无。返回：所有窗口创建成功为 true，否则为 false。
bool AppListPanel::Create() {
    if (!CreateNative(L"DesktopOrbAppList", L"所有应用", WS_POPUP | WS_CLIPCHILDREN,
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 0, 0, kPanelWidth, 600)) return false;
    search_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 1, 1, handle(), nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!search_) return false;
    list_ = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
        LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, 0, 0, 1, 1, handle(), nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!list_) return false;
    if (!SetWindowSubclass(list_, ListSubclass, kListSubclassId,
            reinterpret_cast<DWORD_PTR>(this))) return false;
    SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜索应用名称"));
    return true;
}

// 作用：根据目标显示器工作区设置面板位置和子控件尺寸。
// 参数：无。返回：无；系统拒绝定位时保留当前窗口位置。
void AppListPanel::Position() {
    const UINT dpi = GetDpiForWindow(handle());
    const int width = std::min(platform::Scale(kPanelWidth, dpi),
        static_cast<int>(monitor_.work.right - monitor_.work.left));
    const int height = monitor_.work.bottom - monitor_.work.top;
    const int x = edge_ == Edge::left ? monitor_.work.left : monitor_.work.right - width;
    SetWindowPos(handle(), HWND_TOPMOST, x, monitor_.work.top, width, height, SWP_NOACTIVATE);
    const int padding = platform::Scale(kPadding, dpi);
    const int header = platform::Scale(kHeaderHeight, dpi);
    MoveWindow(search_, padding, platform::Scale(kSearchTop, dpi),
        std::max(1, width - padding * 2), platform::Scale(kSearchHeight, dpi), TRUE);
    MoveWindow(list_, 0, header, width, std::max(1, height - header), TRUE);
    // 固定高度列表在跨屏 DPI 变化后不会再次逐项测量，显式同步物理像素行高。
    SendMessageW(list_, LB_SETITEMHEIGHT, 0, platform::Scale(kRowHeight, dpi));
    // 核心逻辑：输入控件使用面板 DPI 对应的字体，避免高缩放显示器上文字过小。
    HFONT next = CreateFontW(-platform::Scale(15, dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (next) {
        SendMessageW(search_, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
        if (font_) DeleteObject(font_);
        font_ = next;
    }
}

// 作用：展示应用列表并重置上次搜索状态。
// 参数：monitor 为所属显示器；edge 为显示侧。返回：无；枚举失败时面板仍显示错误原因。
void AppListPanel::ShowFor(const platform::Monitor& monitor, Edge edge) {
    if (visible_) return;
    monitor_ = monitor;
    edge_ = edge;
    apps_ = catalog_.List(error_);
    icons_.assign(apps_.size(), nullptr);
    visible_ = true;
    Position();
    SetWindowTextW(search_, L"");
    Filter();
    ShowWindow(handle(), SW_SHOW);
    SetForegroundWindow(handle());
    SetFocus(search_);
    // 核心逻辑：Shell 图标可能需要读取磁盘或激活扩展，绝不在 WM_DRAWITEM 中同步调用。
    icon_worker_ = std::jthread([this](std::stop_token stop) { LoadIcons(stop); });
}

// 作用：隐藏应用面板并释放本次枚举内容。
// 参数：无。返回：无；重复调用不会重复触发关闭回调。
void AppListPanel::Hide() {
    if (!visible_) return;
    visible_ = false;
    pressed_row_ = static_cast<size_t>(-1);
    wheel_remainder_ = 0;
    scrolling_ = false;
    KillTimer(handle(), kScrollSettledTimer);
    ShowWindow(handle(), SW_HIDE);
    icon_worker_.request_stop();
    if (icon_worker_.joinable()) icon_worker_.join();
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    visible_indices_.clear();
    ReleaseIcons();
    apps_.clear();
    error_.clear();
    if (on_closed) on_closed();
}

// 作用：根据搜索框文字更新列表，不重新枚举 AppsFolder。
// 参数：无。返回：无；读取输入失败时按空词显示全部应用。
void AppListPanel::Filter() {
    pressed_row_ = static_cast<size_t>(-1);
    wheel_remainder_ = 0;
    const int length = GetWindowTextLengthW(search_);
    std::wstring query(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(search_, query.data(), length + 1);
    query.resize(static_cast<size_t>(std::max(0, copied)));
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    visible_indices_.clear();
    for (const auto kind : {platform::AppKind::desktop, platform::AppKind::packaged}) {
        bool has_group = false;
        for (size_t index = 0; index < apps_.size(); ++index) {
            if (apps_[index].kind != kind || !Matches(apps_[index].name, query)) continue;
            if (!has_group) {
                const size_t header = kind == platform::AppKind::desktop
                    ? kDesktopHeader : kPackagedHeader;
                const wchar_t* title = kind == platform::AppKind::desktop
                    ? L"桌面程序" : L"打包应用";
                visible_indices_.push_back(header);
                if (SendMessageW(list_, LB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(title)) == LB_ERR)
                    visible_indices_.pop_back();
                has_group = true;
            }
            visible_indices_.push_back(index);
            if (SendMessageW(list_, LB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(apps_[index].name.c_str())) == LB_ERR)
                visible_indices_.pop_back();
        }
    }
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, FALSE);
    RECT header{0, 0, platform::Scale(kPanelWidth, GetDpiForWindow(handle())),
        platform::Scale(kHeaderHeight, GetDpiForWindow(handle()))};
    InvalidateRect(handle(), &header, FALSE);
}

// 作用：释放从 Windows Shell 读取的应用图标。
// 参数：无。返回：无；空句柄安全跳过。
void AppListPanel::ReleaseIcons() {
    for (HICON icon : icons_) if (icon) DestroyIcon(icon);
    icons_.clear();
}

// 作用：在工作线程上依次加载图标，并用消息通知 UI 绘制已经缓存的行。
// 参数：stop 为取消信号。返回：无；已取得但未提交的图标由本方法释放。
void AppListPanel::LoadIcons(std::stop_token stop) {
    for (size_t index = 0; index < apps_.size() && !stop.stop_requested(); ++index) {
        HICON icon = platform::InstalledAppCatalog::LoadIcon(apps_[index]);
        if (stop.stop_requested()) {
            if (icon) DestroyIcon(icon);
            break;
        }
        {
            std::lock_guard lock(icons_mutex_);
            icons_[index] = icon;
        }
        PostMessageW(handle(), kIconReady, static_cast<WPARAM>(index), 0);
    }
}

// 作用：启动鼠标单击的应用行，成功后关闭面板。
// 参数：row 为列表行索引。返回：无；标题行或 Shell 启动失败时保留面板。
void AppListPanel::LaunchRow(size_t row) {
    if (row >= visible_indices_.size()) return;
    const size_t index = visible_indices_[row];
    if (IsHeader(index)) return;
    const auto& app = apps_[index];
    if (platform::InstalledAppCatalog::Launch(app)) Hide();
    else MessageBoxW(handle(), L"Windows 无法启动这个应用。", L"启动失败", MB_OK | MB_ICONWARNING);
}

// 作用：统一处理列表滚轮，并仅在鼠标按下、释放位于同一行时发出启动消息。
// 参数：window 为列表窗口；message 为消息；wparam、lparam 为消息数据；
//       subclass_id 为注册时的标识；reference 为 AppListPanel 地址。
// 返回：原列表窗口过程的返回值；有效点击另外向面板投递行号。
LRESULT CALLBACK AppListPanel::ListSubclass(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    auto* panel = reinterpret_cast<AppListPanel*>(reference);
    if (message == WM_MOUSEWHEEL) {
        // 核心逻辑：列表框默认按系统行数一次跳多行；这里每个完整刻度只移动一行。
        // 保留未满一刻度的触控板增量，避免微小反向输入造成上下跳动。
        panel->pressed_row_ = static_cast<size_t>(-1);
        panel->wheel_remainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
        const int steps = panel->wheel_remainder_ / WHEEL_DELTA;
        panel->wheel_remainder_ -= steps * WHEEL_DELTA;
        if (steps != 0) {
            const int top = static_cast<int>(SendMessageW(window, LB_GETTOPINDEX, 0, 0));
            SendMessageW(window, LB_SETTOPINDEX,
                static_cast<WPARAM>(std::max(0, top - steps)), 0);
            panel->scrolling_ = true;
            SetTimer(panel->handle(), kScrollSettledTimer, kScrollSettleMs, nullptr);
        }
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        const LRESULT hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lparam);
        panel->pressed_row_ = HIWORD(hit) == 0
            ? static_cast<size_t>(LOWORD(hit)) : static_cast<size_t>(-1);
    }
    if (message == WM_VSCROLL || message == WM_CAPTURECHANGED) {
        panel->pressed_row_ = static_cast<size_t>(-1);
    }
    if (message == WM_VSCROLL) {
        panel->scrolling_ = true;
        SetTimer(panel->handle(), kScrollSettledTimer, kScrollSettleMs, nullptr);
    }
    if (message == WM_MOUSEMOVE && (wparam & MK_LBUTTON)) {
        const LRESULT hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lparam);
        if (HIWORD(hit) != 0 || static_cast<size_t>(LOWORD(hit)) != panel->pressed_row_)
            panel->pressed_row_ = static_cast<size_t>(-1);
    }
    // 核心逻辑：列表框释放鼠标捕获时会同步发送 WM_CAPTURECHANGED。
    // 先保存按下行，避免原窗口过程清理捕获状态后丢失有效点击。
    const size_t pressed_row = panel->pressed_row_;
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_LBUTTONUP) {
        const LRESULT hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lparam);
        if (HIWORD(hit) == 0 && static_cast<size_t>(LOWORD(hit)) == pressed_row) {
            PostMessageW(panel->handle(), kAppClicked, LOWORD(hit), 0);
        }
        panel->pressed_row_ = static_cast<size_t>(-1);
    }
    return result;
}

// 作用：绘制应用名称、Shell 图标和当前选择状态。
// 参数：item 为列表控件绘制请求。返回：无；无效尺寸跳过绘制。
void AppListPanel::DrawItem(const DRAWITEMSTRUCT& item) {
    if (item.itemID == static_cast<UINT>(-1)) return;
    const int width = item.rcItem.right - item.rcItem.left;
    const int height = item.rcItem.bottom - item.rcItem.top;
    if (width <= 0 || height <= 0) return;
    // 核心逻辑：先把整行绘制到内存 DC，再一次性提交，滚动时不会露出半绘制内容。
    HDC memory = CreateCompatibleDC(item.hDC);
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(item.hDC, width, height) : nullptr;
    // 核心逻辑：内存位图创建失败时退回直接绘制，避免出现空白行。
    HDC drawing = bitmap ? memory : item.hDC;
    HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : nullptr;
    if (bitmap) SetViewportOrgEx(memory, -item.rcItem.left, -item.rcItem.top, nullptr);
    const size_t index = item.itemID < visible_indices_.size()
        ? visible_indices_[item.itemID] : kDesktopHeader;
    if (IsHeader(index)) {
        Fill(drawing, item.rcItem, RGB(232, 239, 244));
        const std::wstring title = index == kDesktopHeader ? L"桌面程序" : L"打包应用";
        RECT label = item.rcItem;
        label.left += platform::Scale(kPadding, GetDpiForWindow(handle()));
        Text(drawing, title, label, platform::Scale(13, GetDpiForWindow(handle())),
            RGB(75, 93, 108), FW_SEMIBOLD, DT_SINGLELINE | DT_VCENTER);
    } else {
        const bool selected = (item.itemState & ODS_SELECTED) != 0;
        const COLORREF background = selected ? RGB(218, 235, 247) : RGB(248, 250, 252);
        Fill(drawing, item.rcItem, background);
        const UINT dpi = GetDpiForWindow(handle());
        const int padding = platform::Scale(kPadding, dpi);
        const int icon_size = platform::Scale(24, dpi);
        {
            std::lock_guard lock(icons_mutex_);
            if (icons_[index]) DrawIconEx(drawing, item.rcItem.left + padding,
                item.rcItem.top + (item.rcItem.bottom - item.rcItem.top - icon_size) / 2,
                icons_[index], icon_size, icon_size, 0, nullptr, DI_NORMAL);
        }
        RECT label{item.rcItem.left + padding + platform::Scale(38, dpi), item.rcItem.top,
            item.rcItem.right - padding, item.rcItem.bottom};
        Text(drawing, apps_[index].name, label, platform::Scale(15, dpi),
            RGB(38, 48, 59), selected ? FW_SEMIBOLD : FW_NORMAL,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (item.itemState & ODS_FOCUS) DrawFocusRect(drawing, &item.rcItem);
    }
    if (bitmap) {
        BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height,
            memory, item.rcItem.left, item.rcItem.top, SRCCOPY);
        SelectObject(memory, previous);
        DeleteObject(bitmap);
    }
    if (memory) DeleteDC(memory);
}

// 作用：处理面板标题绘制、列表自绘、搜索和启动通知。
// 参数：message 为 Win32 消息；wparam、lparam 为参数。返回：已处理消息为 0，否则为默认结果。
LRESULT AppListPanel::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_MEASUREITEM: {
            auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (item->CtlType == ODT_LISTBOX) {
                // 固定行高让列表框的顶部索引与滚动条位置保持一一对应。
                item->itemHeight = platform::Scale(kRowHeight, GetDpiForWindow(handle()));
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
            if (reinterpret_cast<DRAWITEMSTRUCT*>(lparam)->hwndItem == list_) {
                DrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lparam) == search_ && HIWORD(wparam) == EN_CHANGE) {
                Filter();
                return 0;
            }
            break;
        case kAppClicked:
            if (visible_) LaunchRow(static_cast<size_t>(wparam));
            return 0;
        case kIconReady: {
            if (scrolling_) return 0;
            const size_t index = static_cast<size_t>(wparam);
            const auto found = std::find(visible_indices_.begin(), visible_indices_.end(), index);
            if (visible_ && found != visible_indices_.end()) {
                const int row = static_cast<int>(found - visible_indices_.begin());
                RECT bounds{};
                if (SendMessageW(list_, LB_GETITEMRECT, row,
                        reinterpret_cast<LPARAM>(&bounds)) != LB_ERR)
                    InvalidateRect(list_, &bounds, FALSE);
            }
            return 0;
        }
        case WM_TIMER:
            if (wparam == kScrollSettledTimer) {
                KillTimer(handle(), kScrollSettledTimer);
                scrolling_ = false;
                InvalidateRect(list_, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            // 鼠标在搜索框或标题上滚动时，仍交给列表统一处理。
            SendMessageW(list_, WM_MOUSEWHEEL, wparam, lparam);
            return 0;
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lparam) == search_) {
                SetTextColor(reinterpret_cast<HDC>(wparam), RGB(38, 48, 59));
                SetBkColor(reinterpret_cast<HDC>(wparam), RGB(255, 255, 255));
                return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(handle(), &paint);
            RECT client{};
            GetClientRect(handle(), &client);
            // WS_CLIPCHILDREN 保留子控件像素，重绘标题时只处理标题区域。
            Fill(dc, client, RGB(248, 250, 252));
            const UINT dpi = GetDpiForWindow(handle());
            Fill(dc, {0, 0, client.right, platform::Scale(kHeaderHeight, dpi)}, RGB(30, 42, 47));
            const int padding = platform::Scale(kPadding, dpi);
            Text(dc, L"所有应用", {padding, 8, client.right - padding, platform::Scale(42, dpi)},
                platform::Scale(20, dpi), RGB(246, 251, 249), FW_SEMIBOLD);
            const size_t app_count = std::count_if(visible_indices_.begin(), visible_indices_.end(),
                [](size_t index) { return !IsHeader(index); });
            const std::wstring subtitle = error_.empty()
                ? std::to_wstring(app_count) + L" 个应用 · 点击启动"
                : error_;
            Text(dc, subtitle, {padding, platform::Scale(36, dpi), client.right - padding,
                platform::Scale(60, dpi)}, platform::Scale(12, dpi), RGB(179, 198, 199));
            EndPaint(handle(), &paint);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) { Hide(); return 0; }
            break;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            Position();
            return 0;
        default: break;
    }
    return WindowBase::HandleMessage(message, wparam, lparam);
}

}  // desktop_orb::ui 命名空间结束
