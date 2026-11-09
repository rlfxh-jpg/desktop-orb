// 文件职责：绘制剪贴板历史的可滚动侧边列表，支持搜索文字、图片预览和逐项置顶。
// 列表仅借用历史记录，在剪贴板更新时必须由应用层先更新数据再调用 Refresh。
#include "desktop_orb/ui/clipboard/clipboard_panel.hpp"

#include "desktop_orb/ui/common/drawing.hpp"
#include <algorithm>
#include <commctrl.h>
#include <cwctype>
#include <windowsx.h>

namespace desktop_orb::ui {
namespace {

constexpr int kPanelWidth = 440;
constexpr int kHeaderHeight = 116;
constexpr int kPadding = 18;
constexpr int kRowHeight = 76;
constexpr UINT_PTR kSubclassId = 1;
constexpr UINT kRowClick = WM_APP + 41;

// 作用：进行不区分大小写的文本搜索。参数：text 为历史文本；query 为关键词。
// 返回：包含关键词为 true；空关键词也返回 true。
bool Matches(const std::wstring& text, const std::wstring& query) {
    if (query.empty()) return true;
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
        [](wchar_t left, wchar_t right) { return std::towlower(left) == std::towlower(right); })
        != text.end();
}

}  // 匿名命名空间结束

ClipboardPanel::~ClipboardPanel() {
    if (list_ && IsWindow(list_)) {
        RemoveWindowSubclass(list_, ListSubclass, kSubclassId);
        DestroyWindow(list_);
    }
    if (search_ && IsWindow(search_)) DestroyWindow(search_);
    if (font_) DeleteObject(font_);
}

bool ClipboardPanel::Create() {
    if (!CreateNative(L"DesktopOrbClipboardPanel", L"剪贴板历史",
        WS_POPUP | WS_CLIPCHILDREN, WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        0, 0, kPanelWidth, 600)) return false;
    search_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 1, 1, handle(), nullptr,
        GetModuleHandleW(nullptr), nullptr);
    list_ = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        0, 0, 1, 1, handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!search_ || !list_ || !SetWindowSubclass(list_, ListSubclass, kSubclassId,
        reinterpret_cast<DWORD_PTR>(this))) return false;
    SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"搜索复制过的文字或图片"));
    return true;
}

void ClipboardPanel::Position() {
    const UINT dpi = GetDpiForWindow(handle());
    const int width = std::min(platform::Scale(kPanelWidth, dpi),
        static_cast<int>(monitor_.work.right - monitor_.work.left));
    const int height = monitor_.work.bottom - monitor_.work.top;
    const int x = edge_ == Edge::left ? monitor_.work.left : monitor_.work.right - width;
    SetWindowPos(handle(), HWND_TOPMOST, x, monitor_.work.top, width, height, SWP_NOACTIVATE);
    const int padding = platform::Scale(kPadding, dpi);
    MoveWindow(search_, padding, platform::Scale(62, dpi),
        std::max(1, width - padding * 2), platform::Scale(36, dpi), TRUE);
    const int header = platform::Scale(kHeaderHeight, dpi);
    MoveWindow(list_, 0, header, width, std::max(1, height - header), TRUE);
    SendMessageW(list_, LB_SETITEMHEIGHT, 0, platform::Scale(kRowHeight, dpi));
    HFONT next = CreateFontW(-platform::Scale(15, dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (next) {
        SendMessageW(search_, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
        if (font_) DeleteObject(font_);
        font_ = next;
    }
}

void ClipboardPanel::ShowFor(platform::ClipboardHistory& history,
    const platform::Monitor& monitor, Edge edge) {
    if (visible_) return;
    history_ = &history;
    monitor_ = monitor;
    edge_ = edge;
    previous_foreground_ = GetForegroundWindow();
    visible_ = true;
    Position();
    SetWindowTextW(search_, L"");
    Refresh();
    ShowWindow(handle(), SW_SHOW);
    SetForegroundWindow(handle());
    SetFocus(search_);
}

void ClipboardPanel::Hide() {
    if (!visible_) return;
    visible_ = false;
    ShowWindow(handle(), SW_HIDE);
    visible_entries_.clear();
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    history_ = nullptr;
    if (on_closed) on_closed();
}

void ClipboardPanel::Refresh() {
    if (!visible_ || !history_) return;
    Filter();
}

void ClipboardPanel::Filter() {
    if (!history_) return;
    const int length = GetWindowTextLengthW(search_);
    std::wstring query(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(search_, query.data(), length + 1);
    query.resize(static_cast<size_t>(std::max(0, copied)));
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    visible_entries_.clear();
    for (const auto* entry : history_->Entries()) {
        const std::wstring label = entry->dib.empty()
            ? entry->text : L"图片 " + std::to_wstring(entry->width) + L" × " + std::to_wstring(entry->height);
        if (!Matches(label, query)) continue;
        visible_entries_.push_back(entry);
        if (SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())) == LB_ERR)
            visible_entries_.pop_back();
    }
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, FALSE);
    RECT header{0, 0, platform::Scale(kPanelWidth, GetDpiForWindow(handle())),
        platform::Scale(kHeaderHeight, GetDpiForWindow(handle()))};
    InvalidateRect(handle(), &header, FALSE);
}

LRESULT CALLBACK ClipboardPanel::ListSubclass(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    auto* panel = reinterpret_cast<ClipboardPanel*>(reference);
    if (message == WM_LBUTTONDOWN) {
        const LRESULT hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lparam);
        panel->pressed_row_ = HIWORD(hit) == 0 ? LOWORD(hit) : static_cast<size_t>(-1);
    }
    if (message == WM_MOUSEWHEEL || message == WM_VSCROLL) panel->pressed_row_ = static_cast<size_t>(-1);
    const size_t pressed = panel->pressed_row_;
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_LBUTTONUP) {
        const LRESULT hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lparam);
        if (HIWORD(hit) == 0 && LOWORD(hit) == pressed)
            PostMessageW(panel->handle(), kRowClick, LOWORD(hit), GET_X_LPARAM(lparam));
        panel->pressed_row_ = static_cast<size_t>(-1);
    }
    return result;
}

void ClipboardPanel::DrawRow(const DRAWITEMSTRUCT& item) {
    if (item.itemID >= visible_entries_.size()) return;
    const auto* entry = visible_entries_[item.itemID];
    HDC memory = CreateCompatibleDC(item.hDC);
    const int width = item.rcItem.right - item.rcItem.left;
    const int height = item.rcItem.bottom - item.rcItem.top;
    HBITMAP bitmap = memory && width > 0 && height > 0
        ? CreateCompatibleBitmap(item.hDC, width, height) : nullptr;
    HDC dc = bitmap ? memory : item.hDC;
    HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : nullptr;
    if (bitmap) SetViewportOrgEx(memory, -item.rcItem.left, -item.rcItem.top, nullptr);
    const UINT dpi = GetDpiForWindow(handle());
    Fill(dc, item.rcItem, (item.itemState & ODS_SELECTED) ? RGB(221, 238, 250) : RGB(248, 250, 252));
    RECT preview{item.rcItem.left + platform::Scale(16, dpi), item.rcItem.top + platform::Scale(12, dpi),
        item.rcItem.left + platform::Scale(72, dpi), item.rcItem.bottom - platform::Scale(12, dpi)};
    if (!entry->dib.empty()) {
        const auto* info = reinterpret_cast<const BITMAPINFO*>(entry->dib.data());
        size_t offset = info->bmiHeader.biSize;
        if (info->bmiHeader.biCompression == BI_BITFIELDS && offset == sizeof(BITMAPINFOHEADER))
            offset += 3 * sizeof(DWORD);
        if (info->bmiHeader.biBitCount <= 8) {
            // 核心逻辑：低色深 DIB 的像素位于调色板之后；不计入调色板会把颜色表误绘为图像。
            const DWORD colors = info->bmiHeader.biClrUsed ? info->bmiHeader.biClrUsed
                : (1U << info->bmiHeader.biBitCount);
            offset += static_cast<size_t>(colors) * sizeof(RGBQUAD);
        }
        if (offset < entry->dib.size()) {
            const int old_mode = SetStretchBltMode(dc, HALFTONE);
            StretchDIBits(dc, preview.left, preview.top, preview.right - preview.left,
                preview.bottom - preview.top, 0, 0, entry->width, entry->height,
                entry->dib.data() + offset, info, DIB_RGB_COLORS, SRCCOPY);
            SetStretchBltMode(dc, old_mode);
        }
    } else {
        Fill(dc, preview, RGB(222, 231, 238));
        Text(dc, L"文", preview, platform::Scale(20, dpi), RGB(53, 78, 97),
            FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const std::wstring label = entry->dib.empty() ? entry->text
        : L"图片 " + std::to_wstring(entry->width) + L" × " + std::to_wstring(entry->height);
    RECT text{preview.right + platform::Scale(14, dpi), item.rcItem.top + platform::Scale(8, dpi),
        item.rcItem.right - platform::Scale(55, dpi), item.rcItem.bottom - platform::Scale(8, dpi)};
    Text(dc, label, text, platform::Scale(14, dpi), RGB(37, 50, 61), FW_NORMAL,
        DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT pin{item.rcItem.right - platform::Scale(50, dpi), item.rcItem.top,
        item.rcItem.right - platform::Scale(8, dpi), item.rcItem.bottom};
    Text(dc, entry->pinned ? L"★" : L"☆", pin, platform::Scale(22, dpi),
        RGB(31, 112, 172), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (bitmap) {
        BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height,
            memory, item.rcItem.left, item.rcItem.top, SRCCOPY);
        SelectObject(memory, previous);
        DeleteObject(bitmap);
    }
    if (memory) DeleteDC(memory);
}

LRESULT ClipboardPanel::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_MEASUREITEM: {
            auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (item->CtlType == ODT_LISTBOX) {
                item->itemHeight = platform::Scale(kRowHeight, GetDpiForWindow(handle()));
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
            if (reinterpret_cast<DRAWITEMSTRUCT*>(lparam)->hwndItem == list_) {
                DrawRow(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lparam) == search_ && HIWORD(wparam) == EN_CHANGE) {
                Filter();
                return 0;
            }
            break;
        case kRowClick: {
            const size_t row = static_cast<size_t>(wparam);
            if (!visible_ || !history_ || row >= visible_entries_.size()) return 0;
            const auto id = visible_entries_[row]->id;
            RECT bounds{};
            GetClientRect(list_, &bounds);
            if (static_cast<int>(lparam) >= bounds.right - platform::Scale(55, GetDpiForWindow(handle()))) {
                history_->TogglePin(id);
                Filter();
            } else if (history_->Restore(id)) {
                const HWND previous = previous_foreground_;
                Hide();
                if (previous && IsWindow(previous)) SetForegroundWindow(previous);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(handle(), &paint);
            RECT client{};
            GetClientRect(handle(), &client);
            Fill(dc, client, RGB(248, 250, 252));
            const UINT dpi = GetDpiForWindow(handle());
            Fill(dc, {0, 0, client.right, platform::Scale(kHeaderHeight, dpi)}, RGB(30, 42, 47));
            const int padding = platform::Scale(kPadding, dpi);
            Text(dc, L"剪贴板历史", {padding, 8, client.right - padding, platform::Scale(42, dpi)},
                platform::Scale(20, dpi), RGB(246, 251, 249), FW_SEMIBOLD);
            const std::wstring subtitle = std::to_wstring(visible_entries_.size()) + L" 条 · 点击复制，星标置顶";
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
