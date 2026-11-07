#pragma once

// 文件职责：封装 Win32 窗口注册、创建和消息分发。
// 基类持有 HWND 并在析构时销毁；派生类只实现业务消息处理。
#include <windows.h>

namespace desktop_orb::ui {

class WindowBase {
public:
    // 作用：创建尚未绑定 HWND 的窗口基类。参数：无。返回：无。
    WindowBase() = default;
    // 作用：禁止复制窗口句柄所有权。参数：另一对象。返回：无；接口已删除。
    WindowBase(const WindowBase&) = delete;
    // 作用：禁止复制赋值。参数：另一对象。返回：无；接口已删除。
    WindowBase& operator=(const WindowBase&) = delete;
    // 作用：若窗口仍有效则销毁 HWND。参数：无。返回：无。
    virtual ~WindowBase();

    // 作用：取得当前原生窗口句柄。参数：无。返回：HWND，尚未创建或已销毁时为空。
    [[nodiscard]] HWND handle() const { return hwnd_; }

protected:
    // 作用：注册窗口类并创建绑定到当前对象的 HWND。
    // 参数：class_name 为窗口类名；title 为标题；style、ex_style 为窗口样式；
    //       x、y 为屏幕坐标；width、height 为尺寸。返回：创建成功为 true，否则为 false。
    bool CreateNative(const wchar_t* class_name, const wchar_t* title, DWORD style,
                      DWORD ex_style, int x, int y, int width, int height);
    // 作用：提供派生类未处理消息的默认分发。
    // 参数：message 为消息码；wparam、lparam 为消息参数。返回：DefWindowProcW 的结果。
    virtual LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    // 作用：在 WM_NCCREATE 绑定实例指针，之后转发消息并在销毁时解绑。
    // 参数：hwnd 为消息窗口；message 为消息码；wparam、lparam 为消息参数。
    // 返回：对应实例消息处理的 LRESULT；实例尚不可用时交给系统默认处理。
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    HWND hwnd_ = nullptr;
};

}  // desktop_orb::ui 命名空间结束
