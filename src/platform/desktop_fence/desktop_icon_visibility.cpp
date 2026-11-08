// 文件职责：查找 Explorer 桌面图标视图，并在围栏生命周期内临时隐藏和恢复。
// HWND 属于 Explorer；本模块仅保存句柄与原始可见状态，不销毁或重建 Shell 窗口。
#include "desktop_orb/platform/desktop_fence/desktop_icon_visibility.hpp"

namespace desktop_orb::platform {
namespace {

// 作用：从 Explorer 顶层窗口中收集桌面图标列表控件。
// 参数：window 为当前枚举到的顶层窗口；context 为接收句柄的 vector 指针。
// 返回：继续枚举时为 TRUE；异常情况不终止 EnumWindows。
BOOL CALLBACK CollectDesktopViews(HWND window, LPARAM context) {
    auto* views = reinterpret_cast<std::vector<HWND>*>(context);
    HWND desktop_view = FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr);
    HWND icon_list = desktop_view
        ? FindWindowExW(desktop_view, nullptr, L"SysListView32", nullptr) : nullptr;
    if (icon_list) views->push_back(icon_list);
    return TRUE;
}

}  // 匿名命名空间结束

// 作用：确保对象离开作用域时 Explorer 桌面视图不会遗留为隐藏状态。
// 参数：无。返回：无。
DesktopIconVisibility::~DesktopIconVisibility() {
    Restore();
}

// 作用：暂时隐藏当前可见的桌面图标视图并记住其原始状态。
// 参数：无。返回：找到至少一个桌面视图为 true；没有桌面视图或枚举失败时为 false。
bool DesktopIconVisibility::Hide() {
    if (!views_.empty()) return true;

    std::vector<HWND> windows;
    EnumWindows(CollectDesktopViews, reinterpret_cast<LPARAM>(&windows));
    for (HWND window : windows) {
        if (!IsWindow(window)) continue;
        const bool was_visible = IsWindowVisible(window) != FALSE;
        views_.push_back({window, was_visible});
        if (was_visible) ShowWindow(window, SW_HIDE);
    }
    return !views_.empty();
}

// 作用：恢复每个桌面图标视图隐藏前的可见状态并清除快照。
// 参数：无。返回：无；已失效的 Explorer 窗口会安全跳过。
void DesktopIconVisibility::Restore() {
    for (const ViewState& state : views_) {
        if (!IsWindow(state.window)) continue;
        ShowWindow(state.window, state.was_visible ? SW_SHOWNA : SW_HIDE);
    }
    views_.clear();
}

}  // desktop_orb::platform 命名空间结束
