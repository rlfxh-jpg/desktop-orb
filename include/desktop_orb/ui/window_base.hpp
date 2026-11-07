#pragma once

#include <windows.h>

namespace desktop_orb::ui {

class WindowBase {
public:
    WindowBase() = default;
    WindowBase(const WindowBase&) = delete;
    WindowBase& operator=(const WindowBase&) = delete;
    virtual ~WindowBase();

    [[nodiscard]] HWND handle() const { return hwnd_; }

protected:
    bool CreateNative(const wchar_t* class_name, const wchar_t* title, DWORD style,
                      DWORD ex_style, int x, int y, int width, int height);
    virtual LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    HWND hwnd_ = nullptr;
};

}  // namespace desktop_orb::ui
