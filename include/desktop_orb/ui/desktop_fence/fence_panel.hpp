#pragma once

// 文件职责：声明桌面围栏浏览面板及其 Shell 项目交互。
// 面板拥有 Tab/ListView 子窗口；桌面枚举和打开操作委托给 platform/desktop_fence。
#include "desktop_orb/platform/common/monitor.hpp"
#include "desktop_orb/platform/desktop_fence/desktop_item_catalog.hpp"
#include "desktop_orb/ui/common/window_base.hpp"
#include <commctrl.h>
#include <functional>
#include <vector>

namespace desktop_orb::ui {

class FencePanel final : public WindowBase {
public:
    // 作用：通知应用层桌面围栏面板已关闭。参数：无。返回：无。
    std::function<void()> on_closed;

    // 作用：创建初始隐藏的桌面围栏窗口及分类、项目列表控件。
    // 参数：无。返回：窗口和子控件全部创建成功为 true，否则为 false。
    bool Create();
    // 作用：在指定显示器工作区边缘显示桌面项目面板。
    // 参数：monitor 为悬浮球所在显示器；align_right 为 true 时靠右侧显示。
    // 返回：无；目录读取失败时仍显示空列表供用户刷新或关闭。
    void ShowFor(const platform::Monitor& monitor, bool align_right);
    // 作用：隐藏面板、停止刷新并通知应用恢复悬浮球。
    // 参数：无。返回：无；面板原本隐藏时不重复通知。
    void Hide();
    // 作用：查询面板当前显示状态。参数：无。返回：显示为 true，否则为 false。
    [[nodiscard]] bool visible() const { return visible_; }

protected:
    // 作用：处理窗口布局、项目刷新、分类切换、打开项目和关闭消息。
    // 参数：message 为消息码；wparam、lparam 为消息参数。返回：已处理消息为 0，否则交给基类。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    // 作用：把面板窗口放到指定显示器工作区的目标边缘。
    // 参数：无。返回：无；窗口大小受显示器可用工作区限制。
    void Position();
    // 作用：根据当前选中分类重新枚举并填充桌面项目列表。
    // 参数：无。返回：无；枚举为空时显示空状态文字。
    void Refresh();
    // 作用：按客户区尺寸和 DPI 更新标题、分类标签及列表控件位置。
    // 参数：width、height 为客户区尺寸；dpi 为窗口当前 DPI。返回：无。
    void LayoutControls(int width, int height, UINT dpi);
    // 作用：打开列表中指定的桌面项目并在失败时提示用户。
    // 参数：index 为当前分类项目的零起始索引。返回：无；打开失败时保留面板并显示错误。
    void OpenItem(size_t index);
    // 作用：读取当前选中的项目分类。
    // 参数：无。返回：有效分类；Tab 状态异常时退回“其他”。
    [[nodiscard]] platform::DesktopFenceCategory SelectedCategory() const;

    platform::DesktopItemCatalog catalog_;
    platform::Monitor monitor_{};
    HWND tabs_ = nullptr;
    HWND list_ = nullptr;
    HWND empty_label_ = nullptr;
    HIMAGELIST system_icons_ = nullptr;
    std::vector<platform::DesktopItem> items_;
    bool align_right_ = true;
    bool visible_ = false;
};

}  // desktop_orb::ui 命名空间结束
