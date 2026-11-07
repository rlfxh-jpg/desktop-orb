#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace desktop_orb::platform {

struct WindowInfo {
    HWND handle = nullptr;
    std::wstring title;
    int monitor_number = 1;
    bool minimized = false;
    std::uint64_t last_used = 0;
    size_t z_order = 0;
};

class WindowCatalog {
public:
    void ObserveForeground();
    [[nodiscard]] std::vector<WindowInfo> List(const std::vector<HWND>& excluded) const;
    [[nodiscard]] static bool Activate(HWND window);

private:
    std::unordered_map<HWND, std::uint64_t> recency_;
    HWND last_foreground_ = nullptr;
    std::uint64_t sequence_ = 0;
};

}  // namespace desktop_orb::platform
