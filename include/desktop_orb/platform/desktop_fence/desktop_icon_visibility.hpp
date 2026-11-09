#pragma once

// 文件职责：记录并切换 Windows 桌面图标视图的显示状态。
// 本类只借用 Explorer 桌面视图 HWND；辅助进程在主程序异常退出后恢复图标。
#include <windows.h>
#include <vector>

namespace desktop_orb::platform {

class DesktopIconVisibility final {
public:
    // 作用：恢复此前隐藏的桌面视图并释放状态记录。参数：无。返回：无。
    ~DesktopIconVisibility();

    // 作用：隐藏当前可见的 Explorer 桌面图标视图。
    // 参数：无。返回：记录到桌面视图且保护进程已启动为 true；找不到视图或保护进程启动失败为 false。
    bool Hide();

    // 作用：按 Hide 调用前的可见状态恢复桌面视图。
    // 参数：无。返回：无；Explorer 已重启或句柄失效时跳过该视图，并结束保护进程。
    void Restore();

private:
    struct ViewState {
        HWND window = nullptr;
        bool was_visible = false;
    };

    std::vector<ViewState> views_;
    HANDLE guard_event_ = nullptr;
};

// 作用：作为辅助进程等待主程序退出，并在非正常退出时恢复其隐藏的桌面图标。
// 参数：无；从当前进程命令行读取父进程 ID、继承的事件句柄和桌面图标 HWND。
// 返回：0 表示等待或恢复完成，1 表示参数无效；此函数仅由程序入口的保护模式调用。
int RunDesktopIconGuard();

}  // desktop_orb::platform 命名空间结束
