#pragma once

// 文件职责：管理桌面围栏的四块独立卡片和统一显示、关闭状态。
// 每块卡片自行绘制图标网格并持有 HWND；项目枚举与打开交给 platform/desktop_fence。
#include "desktop_orb/platform/common/monitor.hpp"
#include "desktop_orb/platform/desktop_fence/desktop_icon_visibility.hpp"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace desktop_orb::ui {

class FenceCard;

class FencePanel final {
public:
    // 作用：初始化尚未创建卡片窗口的围栏管理器。参数：无。返回：无。
    FencePanel();
    // 作用：创建四块独立桌面围栏卡片。参数：无。返回：全部创建成功为 true，否则为 false。
    bool Create();
    // 作用：销毁卡片所持有的窗口资源。参数：无。返回：无。
    ~FencePanel();
    // 作用：在悬浮球所在显示器工作区排列并显示所有卡片。
    // 参数：monitor 为显示器；align_right 为悬浮球是否贴靠右边。返回：无。
    void ShowFor(const platform::Monitor& monitor, bool align_right);
    // 作用：隐藏所有卡片并通知应用恢复悬浮球。参数：无。返回：无。
    void Hide();
    // 作用：查询围栏是否显示。参数：无。返回：显示为 true，否则为 false。
    [[nodiscard]] bool visible() const { return visible_; }
    // 作用：收集四块卡片的窗口句柄，供窗口切换器排除本程序窗口。
    // 参数：无。返回：所有已创建的卡片句柄。
    [[nodiscard]] std::vector<HWND> Handles() const;
    // 作用：判断屏幕点是否命中任意一块卡片。
    // 参数：point 为屏幕坐标。返回：命中当前可见卡片时为 true。
    [[nodiscard]] bool Contains(POINT point) const;

    // 作用：通知应用层全部卡片已经关闭。参数：无。返回：无。
    std::function<void()> on_closed;

private:
    std::array<std::unique_ptr<FenceCard>, 4> cards_;
    platform::DesktopIconVisibility desktop_icon_visibility_;
    bool visible_ = false;
};

}  // desktop_orb::ui 命名空间结束
