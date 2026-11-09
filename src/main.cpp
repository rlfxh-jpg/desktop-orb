// 文件职责：程序入口负责系统级单实例检查、界面初始化和消息循环。
// 启动阶段的错误通过对话框反馈，因为 GUI 程序没有附加控制台。
#include "desktop_orb/app.hpp"
#include "desktop_orb/platform/common/single_instance.hpp"
#include "desktop_orb/platform/desktop_fence/desktop_icon_visibility.hpp"
#include <cwchar>

// 作用：启动桌面悬浮球并进入 Win32 消息循环。
// 参数：command_line 为 Windows 提供的命令行参数；其他 WinMain 参数当前不使用。
// 返回：正常退出时为消息循环退出码，单实例检查或初始化失败时为 1；
//       已有实例运行时返回 0，不再创建第二个悬浮球。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int) {
    // 核心逻辑：图标保护进程不创建悬浮球，也不参与单实例互斥锁竞争。
    if (command_line && std::wcsncmp(command_line, L"--desktop-icon-guard ", 21) == 0)
        return desktop_orb::platform::RunDesktopIconGuard();
    desktop_orb::platform::SingleInstance instance;
    if (instance.result() == desktop_orb::platform::SingleInstance::Result::already_running)
        return 0;
    if (instance.result() == desktop_orb::platform::SingleInstance::Result::error) {
        MessageBoxW(nullptr, L"无法确认 Desktop Orb 是否已经运行。", L"Desktop Orb 启动失败", MB_OK | MB_ICONERROR);
        return 1;
    }

    desktop_orb::App app;
    if (!app.Initialize()) {
        MessageBoxW(nullptr, L"无法初始化桌面悬浮球或功能菜单。请检查程序旁的 assets\\icons\\ 图片文件。",
                    L"Desktop Orb 启动失败", MB_OK | MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
