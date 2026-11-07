#pragma once

// 文件职责：管理围绕悬浮球显示的功能按钮及其点击回调。
// 按钮窗口由菜单对象持有，具体功能由外部回调实现。
#include "desktop_orb/platform/monitor.hpp"
#include "desktop_orb/ui/window_base.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace desktop_orb::ui {

class FeatureMenu {
public:
    // 作用：创建空功能菜单。参数：无。返回：无。
    FeatureMenu();
    // 作用：销毁菜单持有的按钮窗口。参数：无。返回：无。
    ~FeatureMenu();
    // 作用：创建并注册一个功能按钮。
    // 参数：title 为悬停 3 秒后显示的提示名称；icon_filename 为 assets/icons 中的 PNG 文件名；
    //       action 为点击后执行的回调。
    // 返回：按钮窗口创建成功为 true，否则为 false。
    bool Add(std::wstring title, const wchar_t* icon_filename, std::function<void()> action);
    // 作用：按悬浮球位置在屏幕内侧排列并显示功能按钮。
    // 参数：orb 为悬浮球屏幕矩形；monitor 为所在显示器；dpi 为目标缩放值。返回：无。
    void ShowAround(RECT orb, const platform::Monitor& monitor, UINT dpi);
    // 作用：隐藏所有功能按钮。参数：无。返回：无。
    void Hide();
    // 作用：查询菜单当前是否显示。参数：无。返回：显示为 true，否则为 false。
    [[nodiscard]] bool visible() const { return visible_; }
    // 作用：收集按钮 HWND 以供窗口目录排除或点击判断。参数：无。
    // 返回：当前菜单拥有的按钮句柄列表。
    [[nodiscard]] std::vector<HWND> Handles() const;

private:
    class Button;
    std::vector<std::unique_ptr<Button>> buttons_;
    bool visible_ = false;
};

}  // desktop_orb::ui 命名空间结束
