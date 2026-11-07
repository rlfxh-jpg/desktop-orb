#pragma once

// 文件职责：筛选可切换的 Windows 应用窗口，记录最近使用顺序并执行激活。
// 该模块不创建预览画面，也不负责界面绘制。
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
    // 作用：记录当前前台窗口的最近使用序号。参数：无。返回：无。
    void ObserveForeground();
    // 作用：枚举所有显示器上的可切换应用窗口并排序。
    // 参数：excluded 为本程序自身及其他需排除的 HWND。
    // 返回：窗口信息列表；无符合条件窗口时为空。
    [[nodiscard]] std::vector<WindowInfo> List(const std::vector<HWND>& excluded) const;
    // 作用：恢复最小化窗口并尝试切换到前台。
    // 参数：window 为待激活 HWND。返回：系统接受前台切换为 true，否则为 false。
    [[nodiscard]] static bool Activate(HWND window);

private:
    std::unordered_map<HWND, std::uint64_t> recency_;
    HWND last_foreground_ = nullptr;
    std::uint64_t sequence_ = 0;
};

}  // desktop_orb::platform 命名空间结束
