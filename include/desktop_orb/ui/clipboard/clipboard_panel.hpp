#pragma once

// 文件职责：显示可搜索、可置顶的剪贴板文字和图片历史，提供单击恢复操作。
// 面板借用 ClipboardHistory 条目；子控件及字体由面板持有，记录始终由平台模块持有。
#include "desktop_orb/platform/clipboard/clipboard_history.hpp"
#include "desktop_orb/platform/common/monitor.hpp"
#include "desktop_orb/ui/common/window_base.hpp"
#include <functional>
#include <vector>

namespace desktop_orb::ui {

class ClipboardPanel final : public WindowBase {
public:
    enum class Edge { left, right };
    // 作用：释放字体和子窗口。参数：无。返回：无。
    ~ClipboardPanel() override;
    // 作用：创建初始隐藏的历史面板和搜索控件。参数：无。返回：全部窗口创建成功为 true。
    bool Create();
    // 作用：在指定显示器一侧显示历史。参数：history 为持续有效的记录源；monitor 为显示器；edge 为侧边。
    // 返回：无；历史为空时仍显示搜索与说明。
    void ShowFor(platform::ClipboardHistory& history, const platform::Monitor& monitor, Edge edge);
    // 作用：关闭面板。参数：无。返回：无；只调用一次关闭回调。
    void Hide();
    // 作用：在剪贴板或置顶状态变化后重建可见列表。参数：无。返回：无。
    void Refresh();
    // 作用：查询显示状态。参数：无。返回：面板可见时为 true。
    [[nodiscard]] bool visible() const { return visible_; }
    // 作用：通知应用层面板关闭。参数：无。返回：无。
    std::function<void()> on_closed;

protected:
    // 作用：处理搜索、列表绘制及点击恢复。参数：message 为消息；wparam、lparam 为附带数据。
    // 返回：已处理时为 0 或控件约定结果，否则为默认窗口处理值。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    // 作用：布局面板与子控件。参数：无。返回：无。
    void Position();
    // 作用：按搜索词重建索引。参数：无。返回：无。
    void Filter();
    // 作用：绘制历史行及图片缩略图。参数：item 为列表绘制请求。返回：无。
    void DrawRow(const DRAWITEMSTRUCT& item);
    // 作用：将列表完整点击交给恢复或置顶操作。
    // 参数：window 为列表窗口；message 为消息；wparam、lparam 为消息数据；id 为子类标识；reference 为面板地址。
    // 返回：列表原窗口过程结果。
    static LRESULT CALLBACK ListSubclass(HWND window, UINT message, WPARAM wparam,
        LPARAM lparam, UINT_PTR id, DWORD_PTR reference);

    platform::ClipboardHistory* history_ = nullptr;
    platform::Monitor monitor_{};
    Edge edge_ = Edge::left;
    HWND search_ = nullptr;
    HWND list_ = nullptr;
    HFONT font_ = nullptr;
    HWND previous_foreground_ = nullptr;
    std::vector<const platform::ClipboardEntry*> visible_entries_;
    size_t pressed_row_ = static_cast<size_t>(-1);
    bool visible_ = false;
};

}  // desktop_orb::ui 命名空间结束
