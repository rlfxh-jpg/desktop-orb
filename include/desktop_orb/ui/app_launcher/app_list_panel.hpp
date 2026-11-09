#pragma once

// 文件职责：声明“所有应用”侧边面板，负责搜索、滚动和启动交互。
// 面板持有自身 HWND、子控件和应用图标；应用枚举与启动交给 platform/app_launcher。
#include "desktop_orb/platform/app_launcher/installed_app_catalog.hpp"
#include "desktop_orb/platform/common/monitor.hpp"
#include "desktop_orb/ui/common/window_base.hpp"
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace desktop_orb::ui {

class AppListPanel final : public WindowBase {
public:
    enum class Edge { left, right };

    // 作用：释放本面板持有的搜索字体和 Shell 图标。参数：无。返回：无。
    ~AppListPanel() override;
    // 作用：创建初始隐藏的面板及搜索框、应用列表。
    // 参数：无。返回：所有窗口均创建成功时为 true，否则为 false。
    bool Create();
    // 作用：在指定显示器同侧显示所有已安装应用。
    // 参数：monitor 为悬浮球所在显示器；edge 为面板所在侧。返回：无；枚举失败时显示错误文本。
    void ShowFor(const platform::Monitor& monitor, Edge edge);
    // 作用：隐藏面板并释放本次枚举的应用图标。参数：无。返回：无。
    void Hide();
    // 作用：查询当前面板是否可见。参数：无。返回：可见时为 true。
    [[nodiscard]] bool visible() const { return visible_; }

    // 作用：通知应用层面板已经关闭。参数：无。返回：无。
    std::function<void()> on_closed;

protected:
    // 作用：处理绘制、搜索输入和列表单击启动。
    // 参数：message 为窗口消息；wparam、lparam 为消息参数。返回：已处理消息为 0，否则交给基类。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    // 作用：按当前显示器工作区定位面板并布局子控件。参数：无。返回：无。
    void Position();
    // 作用：按搜索词重建当前列表。参数：无。返回：无；空词显示所有应用。
    void Filter();
    // 作用：释放当前缓存的所有图标句柄。参数：无。返回：无。
    void ReleaseIcons();
    // 作用：启动列表中当前选中的应用。
    // 参数：无。返回：无；Shell 拒绝启动时保留面板并提示。
    void LaunchSelected();
    // 作用：绘制单个拥有自绘样式的应用行。
    // 参数：item 为 Windows 提供的绘制数据。返回：无；无效索引时只绘制背景。
    void DrawItem(const DRAWITEMSTRUCT& item);
    // 作用：后台读取 Shell 图标并通知界面重绘，避免滚动时阻塞绘制消息。
    // 参数：stop 为面板关闭时的取消信号。返回：无；失败图标保留为空。
    void LoadIcons(std::stop_token stop);

    platform::InstalledAppCatalog catalog_;
    platform::Monitor monitor_{};
    Edge edge_ = Edge::left;
    HWND search_ = nullptr;
    HWND list_ = nullptr;
    HFONT font_ = nullptr;
    std::vector<platform::InstalledApp> apps_;
    std::vector<size_t> visible_indices_;
    std::vector<HICON> icons_;
    std::mutex icons_mutex_;
    std::jthread icon_worker_;
    std::wstring error_;
    bool visible_ = false;
};

}  // desktop_orb::ui 命名空间结束
