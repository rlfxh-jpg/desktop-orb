#pragma once

// 文件职责：管理跨显示器窗口列表、滚动选择及实时缩略图面板。
// 窗口枚举与激活委托平台层，关闭事件通过回调通知 App。
#include "desktop_orb/platform/monitor.hpp"
#include "desktop_orb/platform/thumbnail_manager.hpp"
#include "desktop_orb/platform/window_catalog.hpp"
#include "desktop_orb/ui/window_base.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace desktop_orb::ui {

class SwitcherPanel final : public WindowBase {
public:
    enum class Edge { left, right };

    // 作用：将面板关联到窗口目录，供刷新和激活使用。
    // 参数：catalog 为生命周期长于面板的窗口目录。返回：无。
    explicit SwitcherPanel(platform::WindowCatalog& catalog) : catalog_(catalog) {}

    std::function<void()> on_closed;
    // 作用：创建初始隐藏的面板窗口。参数：无。返回：创建成功为 true，否则为 false。
    bool Create();
    // 作用：设置枚举窗口时排除的本程序窗口。
    // 参数：handles 为需排除的 HWND 列表。返回：无。
    void SetExcluded(std::vector<HWND> handles);
    // 作用：在指定显示器边缘展开并刷新面板。
    // 参数：monitor 为悬浮球所在显示器；edge 为展开侧。返回：无。
    void ShowFor(const platform::Monitor& monitor, Edge edge);
    // 作用：关闭面板、释放实时缩略图并通知调用方。参数：无。返回：无。
    void Hide();
    // 作用：查询面板是否显示。参数：无。返回：显示为 true，否则为 false。
    [[nodiscard]] bool visible() const { return visible_; }

protected:
    // 作用：响应面板绘制、滚动、键盘、计时器和显示器变化消息。
    // 参数：message 为消息码；wparam、lparam 为消息参数。
    // 返回：已处理消息为 0，否则为基类处理结果。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    // 作用：将面板放置到所选显示器边缘。参数：无。返回：无。
    void Position();
    // 作用：重新读取可切换窗口并更新显示。参数：无。返回：无。
    void Refresh();
    // 作用：把列表滚动到合法范围内的指定位置。
    // 参数：position 为期望的垂直像素偏移。返回：无。
    void ScrollTo(int position);
    // 作用：相对当前项移动选择并保证目标可见。
    // 参数：direction 为正或负的项偏移。返回：无。
    void SelectRelative(int direction);
    // 作用：尝试激活列表中指定应用窗口。
    // 参数：index 为列表索引。返回：无；失败时保持面板并反馈。
    void Activate(size_t index);
    // 作用：同步当前视口内的 DWM 实时缩略图。参数：无。返回：无。
    void SyncThumbnails();
    // 作用：绘制面板内容及不可预览项占位。参数：无。返回：无。
    void Paint();
    // 作用：计算当前 DPI 下的面板标题栏高度。参数：无。返回：像素高度。
    [[nodiscard]] int Header() const;
    // 作用：计算当前 DPI 下的预览项高度。参数：无。返回：像素高度。
    [[nodiscard]] int CardHeight() const;
    // 作用：计算列表可见区域高度。参数：无。返回：至少为 1 的像素高度。
    [[nodiscard]] int ViewportHeight() const;

    platform::WindowCatalog& catalog_;
    std::unique_ptr<platform::ThumbnailManager> thumbnails_;
    std::vector<platform::WindowInfo> windows_;
    std::vector<HWND> excluded_;
    platform::Monitor monitor_{};
    Edge edge_ = Edge::left;
    HWND active_before_open_ = nullptr;
    int scroll_ = 0;
    int wheel_remainder_ = 0;
    size_t selected_ = 0;
    bool visible_ = false;
};

}  // desktop_orb::ui 命名空间结束
