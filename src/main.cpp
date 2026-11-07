#include "desktop_orb/app.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    desktop_orb::App app;
    if (!app.Initialize()) {
        MessageBoxW(nullptr, L"无法创建桌面悬浮球窗口。", L"Desktop Orb 启动失败", MB_OK | MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
