#include "desktop_orb/ui/switcher_panel.hpp"

#include "desktop_orb/ui/drawing.hpp"
#include <algorithm>
#include <climits>
#include <string>
#include <windowsx.h>

namespace desktop_orb::ui {
namespace {

constexpr UINT_PTR kRefreshTimer = 2;
constexpr int kLogicalHeader = 72;
constexpr int kLogicalCard = 262;
constexpr int kLogicalPreviewHeight = 180;
constexpr int kLogicalPadding = 16;

HICON WindowIcon(HWND window) {
    HICON icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL2, 0));
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
    return icon;
}

}  // namespace

bool SwitcherPanel::Create() {
    if (!CreateNative(L"DesktopOrbSwitcher", L"窗口切换", WS_POPUP | WS_VSCROLL,
                      WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 0, 0, 390, 600)) return false;
    thumbnails_ = std::make_unique<platform::ThumbnailManager>(handle());
    return true;
}

void SwitcherPanel::SetExcluded(std::vector<HWND> handles) {
    excluded_ = std::move(handles);
}

int SwitcherPanel::Header() const { return platform::Scale(kLogicalHeader, GetDpiForWindow(handle())); }
int SwitcherPanel::CardHeight() const { return platform::Scale(kLogicalCard, GetDpiForWindow(handle())); }
int SwitcherPanel::ViewportHeight() const {
    RECT client{};
    GetClientRect(handle(), &client);
    return std::max(1, static_cast<int>(client.bottom) - Header());
}

void SwitcherPanel::Position() {
    const int work_width = monitor_.work.right - monitor_.work.left;
    const int width = std::min(work_width, platform::Scale(390, GetDpiForWindow(handle())));
    const int height = monitor_.work.bottom - monitor_.work.top;
    SetWindowPos(handle(), HWND_TOPMOST, monitor_.work.left, monitor_.work.top,
                 width, height, SWP_NOACTIVATE);
}

void SwitcherPanel::ShowFor(const platform::Monitor& monitor) {
    if (visible_) { Hide(); return; }
    monitor_ = monitor;
    active_before_open_ = nullptr;
    selected_ = 0;
    scroll_ = 0;
    visible_ = true;
    Position();
    ShowWindow(handle(), SW_SHOW);
    SetForegroundWindow(handle());
    Refresh();
    if (!windows_.empty()) active_before_open_ = windows_.front().handle;
    SetTimer(handle(), kRefreshTimer, 800, nullptr);
}

void SwitcherPanel::Hide() {
    if (!visible_) return;
    visible_ = false;
    KillTimer(handle(), kRefreshTimer);
    thumbnails_->Clear();
    windows_.clear();
    ShowWindow(handle(), SW_HIDE);
    if (on_closed) on_closed();
}

void SwitcherPanel::Refresh() {
    if (!visible_) return;
    windows_ = catalog_.List(excluded_);
    if (!windows_.empty()) selected_ = std::min(selected_, windows_.size() - 1);
    else selected_ = 0;
    ScrollTo(scroll_);
}

void SwitcherPanel::ScrollTo(int position) {
    const int content = static_cast<int>(windows_.size()) * CardHeight();
    scroll_ = std::clamp(position, 0, std::max(0, content - ViewportHeight()));
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, content - 1);
    info.nPage = static_cast<UINT>(ViewportHeight());
    info.nPos = scroll_;
    SetScrollInfo(handle(), SB_VERT, &info, TRUE);
    SyncThumbnails();
    InvalidateRect(handle(), nullptr, FALSE);
}

void SwitcherPanel::SelectRelative(int direction) {
    if (windows_.empty()) return;
    selected_ = static_cast<size_t>(std::clamp(static_cast<int>(selected_) + direction,
                                               0, static_cast<int>(windows_.size()) - 1));
    const int top = static_cast<int>(selected_) * CardHeight();
    const int bottom = top + CardHeight();
    if (top < scroll_) ScrollTo(top);
    else if (bottom > scroll_ + ViewportHeight()) ScrollTo(bottom - ViewportHeight());
    else InvalidateRect(handle(), nullptr, FALSE);
}

void SwitcherPanel::Activate(size_t index) {
    if (index >= windows_.size()) return;
    const HWND window = windows_[index].handle;
    if (!IsWindow(window)) { Refresh(); return; }
    if (platform::WindowCatalog::Activate(window)) Hide();
    else MessageBoxW(handle(), L"Windows 暂时不允许将该窗口置于前台。请再试一次。",
                     L"无法切换窗口", MB_OK | MB_ICONWARNING);
}

void SwitcherPanel::SyncThumbnails() {
    if (!visible_) return;
    RECT client{};
    GetClientRect(handle(), &client);
    const UINT dpi = GetDpiForWindow(handle());
    const int pad = platform::Scale(kLogicalPadding, dpi);
    const int preview_height = platform::Scale(kLogicalPreviewHeight, dpi);
    const int card_height = CardHeight();
    const RECT clip{0, Header(), client.right, client.bottom};
    std::vector<platform::ThumbnailRequest> requests;
    for (size_t index = 0; index < windows_.size(); ++index) {
        const auto& window = windows_[index];
        if (window.minimized) continue;
        const int y = Header() + static_cast<int>(index) * card_height - scroll_;
        const RECT preview{pad, y + pad, client.right - pad, y + pad + preview_height};
        if (preview.bottom <= clip.top || preview.top >= clip.bottom) continue;
        requests.push_back({window.handle, preview, clip});
    }
    thumbnails_->Sync(requests);
}

void SwitcherPanel::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(handle(), &paint);
    RECT client{};
    GetClientRect(handle(), &client);
    const UINT dpi = GetDpiForWindow(handle());
    const int pad = platform::Scale(kLogicalPadding, dpi);
    const int preview_height = platform::Scale(kLogicalPreviewHeight, dpi);
    const int card_height = CardHeight();
    Fill(dc, client, Color(239, 244, 243));
    Fill(dc, {0, 0, client.right, Header()}, Color(30, 42, 47));
    Text(dc, L"窗口切换", {pad, platform::Scale(10, dpi), client.right - pad, platform::Scale(43, dpi)},
         platform::Scale(19, dpi), Color(246, 251, 249), FW_SEMIBOLD);
    Text(dc, std::to_wstring(windows_.size()) + L" 个窗口 · 所有显示器",
         {pad, platform::Scale(42, dpi), client.right - pad, Header() - platform::Scale(5, dpi)},
         platform::Scale(11, dpi), Color(168, 195, 191));

    if (windows_.empty()) {
        Text(dc, L"没有可切换的应用窗口", {pad, Header() + pad, client.right - pad, Header() + platform::Scale(100, dpi)},
             platform::Scale(14, dpi), Color(88, 108, 111), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const int saved_dc = SaveDC(dc);
    IntersectClipRect(dc, 0, Header(), client.right, client.bottom);
    for (size_t index = 0; index < windows_.size(); ++index) {
        const auto& window = windows_[index];
        const int y = Header() + static_cast<int>(index) * card_height - scroll_;
        if (y >= client.bottom || y + card_height <= Header()) continue;
        const RECT card{pad / 2, y + pad / 3, client.right - pad / 2, y + card_height - pad / 3};
        Fill(dc, card, Color(255, 255, 255));
        if (index == selected_) Fill(dc, {card.left, card.top, card.left + platform::Scale(3, dpi), card.bottom}, Color(23, 149, 126));
        const RECT preview{pad, y + pad, client.right - pad, y + pad + preview_height};
        Fill(dc, preview, Color(219, 228, 229));
        if (window.minimized || thumbnails_->Failed(window.handle)) {
            Text(dc, window.minimized ? L"窗口已最小化" : L"预览不可用", preview,
                 platform::Scale(13, dpi), Color(96, 116, 119), FW_NORMAL,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        const int label_y = preview.bottom + platform::Scale(8, dpi);
        const HICON icon = WindowIcon(window.handle);
        if (icon) DrawIconEx(dc, pad, label_y, icon, platform::Scale(20, dpi),
                             platform::Scale(20, dpi), 0, nullptr, DI_NORMAL);
        const int text_x = pad + platform::Scale(28, dpi);
        Text(dc, window.title, {text_x, label_y, client.right - pad, label_y + platform::Scale(24, dpi)},
             platform::Scale(13, dpi), Color(33, 49, 54), FW_SEMIBOLD,
             DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        std::wstring badge = L"显示器 " + std::to_wstring(window.monitor_number);
        if (window.handle == active_before_open_) badge += L" · 当前";
        Text(dc, badge, {text_x, label_y + platform::Scale(25, dpi), client.right - pad,
                          label_y + platform::Scale(45, dpi)},
             platform::Scale(10, dpi), Color(93, 111, 115));
    }
    RestoreDC(dc, saved_dc);
    EndPaint(handle(), &paint);
}

LRESULT SwitcherPanel::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: Paint(); SyncThumbnails(); return 0;
        case WM_TIMER: if (wparam == kRefreshTimer) Refresh(); return 0;
        case WM_MOUSEWHEEL: {
            wheel_remainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
            const int steps = wheel_remainder_ / WHEEL_DELTA;
            wheel_remainder_ -= steps * WHEEL_DELTA;
            if (steps) ScrollTo(scroll_ - steps * CardHeight() / 2);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(handle(), SB_VERT, &info);
            int next = scroll_;
            switch (LOWORD(wparam)) {
                case SB_LINEUP: next -= CardHeight() / 4; break;
                case SB_LINEDOWN: next += CardHeight() / 4; break;
                case SB_PAGEUP: next -= ViewportHeight(); break;
                case SB_PAGEDOWN: next += ViewportHeight(); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: next = info.nTrackPos; break;
                case SB_TOP: next = 0; break;
                case SB_BOTTOM: next = INT_MAX; break;
                default: break;
            }
            ScrollTo(next);
            return 0;
        }
        case WM_LBUTTONUP: {
            const int y = GET_Y_LPARAM(lparam);
            if (y >= Header()) Activate(static_cast<size_t>((y - Header() + scroll_) / CardHeight()));
            return 0;
        }
        case WM_KEYDOWN:
            switch (wparam) {
                case VK_ESCAPE: Hide(); break;
                case VK_UP: SelectRelative(-1); break;
                case VK_DOWN: SelectRelative(1); break;
                case VK_RETURN: Activate(selected_); break;
                case VK_PRIOR: ScrollTo(scroll_ - ViewportHeight()); break;
                case VK_NEXT: ScrollTo(scroll_ + ViewportHeight()); break;
                default: break;
            }
            return 0;
        case WM_DPICHANGED: Position(); Refresh(); return 0;
        case WM_DISPLAYCHANGE: Position(); Refresh(); return 0;
        default: return WindowBase::HandleMessage(message, wparam, lparam);
    }
}

}  // namespace desktop_orb::ui
