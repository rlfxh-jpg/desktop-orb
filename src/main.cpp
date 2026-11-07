#include "desktop_orb/app.hpp"
#include "desktop_orb/platform/single_instance.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    desktop_orb::platform::SingleInstance instance;
    if (instance.result() == desktop_orb::platform::SingleInstance::Result::already_running)
        return 0;
    if (instance.result() == desktop_orb::platform::SingleInstance::Result::error) {
        MessageBoxW(nullptr, L"无法确认 Desktop Orb 是否已经运行。", L"Desktop Orb 启动失败", MB_OK | MB_ICONERROR);
        return 1;
    }

    desktop_orb::App app;
    if (!app.Initialize()) {
        MessageBoxW(nullptr, L"无法创建桌面悬浮球窗口。", L"Desktop Orb 启动失败", MB_OK | MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
