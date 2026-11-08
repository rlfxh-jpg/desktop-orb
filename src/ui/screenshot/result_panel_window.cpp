// 文件职责：实现截图分析结果的独立置顶窗口及可滚动文本控件。
// 结果窗口由选区窗口拥有，但使用普通 Win32 绘制，不参与分层截图位图合成。
#include "desktop_orb/ui/screenshot/result_panel_window.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include "desktop_orb/ui/common/drawing.hpp"

#include <utility>

namespace desktop_orb::ui {
namespace {

constexpr int kPanelPadding = 10;
constexpr int kHeaderHeight = 34;
constexpr int kCopyButtonWidth = 88;
constexpr int kCopyButtonHeight = 26;
constexpr int kCopyButtonId = 1;

}  // 匿名命名空间结束

bool ResultPanelWindow::Create(HWND owner) {
    if (!CreateNative(L"DesktopOrbScreenshotResult", L"截图分析结果",
            WS_POPUP | WS_BORDER, WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            0, 0, 1, 1)) {
        return false;
    }
    SetWindowLongPtrW(handle(), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
            ES_NOHIDESEL | WS_TABSTOP,
        0, 0, 1, 1, handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
    copy_button_ = CreateWindowExW(0, L"BUTTON", L"复制结果",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 1, 1, handle(), reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyButtonId)),
        GetModuleHandleW(nullptr), nullptr);
    if (!edit_ || !copy_button_) {
        DestroyWindow(handle());
        return false;
    }
    const WPARAM font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(edit_, WM_SETFONT, font, TRUE);
    SendMessageW(copy_button_, WM_SETFONT, font, TRUE);
    return true;
}

void ResultPanelWindow::SetContent(std::wstring title, std::wstring text) {
    title_ = std::move(title);
    content_ = std::move(text);
    if (edit_) {
        SetWindowTextW(edit_, content_.c_str());
        SendMessageW(edit_, EM_SETSEL, 0, 0);
        SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
    }
    if (handle()) InvalidateRect(handle(), nullptr, FALSE);
}

void ResultPanelWindow::ShowAt(RECT bounds) {
    if (!handle() || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;

    RECT current{};
    const bool is_visible = IsWindowVisible(handle()) != FALSE;
    const bool has_current_bounds = GetWindowRect(handle(), &current) != FALSE;
    const bool same_bounds = has_current_bounds && current.left == bounds.left &&
        current.top == bounds.top && current.right == bounds.right &&
        current.bottom == bounds.bottom;
    if (is_visible && same_bounds) return;

    const bool size_changed = !has_current_bounds ||
        current.right - current.left != bounds.right - bounds.left ||
        current.bottom - current.top != bounds.bottom - bounds.top;
    if (!SetWindowPos(handle(), nullptr, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW)) {
        ShowWindow(handle(), SW_HIDE);
        return;
    }
    if (size_changed) {
        RECT client{};
        GetClientRect(handle(), &client);
        LayoutControls(client.right - client.left, client.bottom - client.top,
                       GetDpiForWindow(handle()));
    }
    InvalidateRect(handle(), nullptr, FALSE);
}

void ResultPanelWindow::Hide() {
    if (handle()) ShowWindow(handle(), SW_HIDE);
}

void ResultPanelWindow::LayoutControls(int width, int height, UINT dpi) {
    const int padding = platform::Scale(kPanelPadding, dpi);
    const int header = platform::Scale(kHeaderHeight, dpi);
    const int button_width = platform::Scale(kCopyButtonWidth, dpi);
    const int button_height = platform::Scale(kCopyButtonHeight, dpi);
    const int button_top = platform::Scale(4, dpi);
    if (edit_) {
        SetWindowPos(edit_, nullptr, padding, header,
            width - padding * 2, height - header - padding,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (copy_button_) {
        SetWindowPos(copy_button_, nullptr, width - padding - button_width, button_top,
            button_width, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LRESULT ResultPanelWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(handle(), &paint);
            RECT client{};
            GetClientRect(handle(), &client);
            Fill(dc, client, Color(29, 37, 40));
            const UINT dpi = GetDpiForWindow(handle());
            const int padding = platform::Scale(kPanelPadding, dpi);
            const int button_reserved = platform::Scale(kCopyButtonWidth + 12, dpi);
            Text(dc, title_, {padding, 0, client.right - button_reserved, client.bottom},
                 platform::Scale(13, dpi), Color(255, 255, 255), FW_SEMIBOLD,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
        case WM_COMMAND:
            if (LOWORD(wparam) == kCopyButtonId && HIWORD(wparam) == BN_CLICKED) {
                if (on_copy) on_copy();
                return 0;
            }
            break;
        default:
            break;
    }
    return WindowBase::HandleMessage(message, wparam, lparam);
}

}  // desktop_orb::ui 命名空间结束
