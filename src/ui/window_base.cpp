#include "desktop_orb/ui/window_base.hpp"

namespace desktop_orb::ui {

WindowBase::~WindowBase() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool WindowBase::CreateNative(const wchar_t* class_name, const wchar_t* title, DWORD style,
                              DWORD ex_style, int x, int y, int width, int height) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = class_name;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    return CreateWindowExW(ex_style, class_name, title, style, x, y, width, height,
                           nullptr, nullptr, instance, this) != nullptr;
}

LRESULT WindowBase::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

LRESULT CALLBACK WindowBase::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    WindowBase* self = reinterpret_cast<WindowBase*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<WindowBase*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
    const LRESULT result = self->HandleMessage(message, wparam, lparam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
    }
    return result;
}

}  // namespace desktop_orb::ui
