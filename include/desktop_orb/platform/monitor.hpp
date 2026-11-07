#pragma once

#include <windows.h>
#include <vector>

namespace desktop_orb::platform {

struct Monitor {
    HMONITOR handle = nullptr;
    RECT bounds{};
    RECT work{};
    UINT dpi = 96;
};

[[nodiscard]] Monitor MonitorAt(POINT point);
[[nodiscard]] Monitor MonitorOf(HWND window);
[[nodiscard]] std::vector<Monitor> AllMonitors();
[[nodiscard]] int Scale(int pixels, UINT dpi);

}  // namespace desktop_orb::platform
