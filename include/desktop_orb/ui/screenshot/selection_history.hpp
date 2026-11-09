#pragma once

// 文件职责：保存截图选区与标注操作的有界撤销、重做历史。
// 模块只管理值对象，不持有截图快照、窗口句柄或 GDI 资源。
#include "desktop_orb/platform/screenshot/screenshot_edits.hpp"
#include <vector>

namespace desktop_orb::ui {

// 作用：表示一次已完成的选区和标注状态。
// 参数：无。返回：无；所有坐标分别为屏幕矩形或相对选区的编辑坐标。
struct ScreenshotSelectionState {
    RECT selection{};
    bool selected = false;
    platform::ScreenshotEdits edits;
};

class ScreenshotSelectionHistory final {
public:
    // 作用：清空本次截图的撤销和重做记录。参数：无。返回：无。
    void Reset();
    // 作用：记录一次实际发生变化的操作，并清除后续重做分支。
    // 参数：before、after 为操作前后的值状态。返回：状态变化且已记录时为 true。
    bool Commit(const ScreenshotSelectionState& before, const ScreenshotSelectionState& after);
    // 作用：把当前状态加入重做栈，并取得上一状态。
    // 参数：current 为当前状态；restored 接收撤销后的状态。返回：有可撤销记录时为 true。
    bool Undo(const ScreenshotSelectionState& current, ScreenshotSelectionState& restored);
    // 作用：把当前状态加入撤销栈，并取得被撤销的下一状态。
    // 参数：current 为当前状态；restored 接收重做后的状态。返回：有可重做记录时为 true。
    bool Redo(const ScreenshotSelectionState& current, ScreenshotSelectionState& restored);
    // 作用：查询是否可撤销。参数：无。返回：撤销栈非空为 true。
    [[nodiscard]] bool can_undo() const { return !undo_.empty(); }
    // 作用：查询是否可重做。参数：无。返回：重做栈非空为 true。
    [[nodiscard]] bool can_redo() const { return !redo_.empty(); }

private:
    std::vector<ScreenshotSelectionState> undo_;
    std::vector<ScreenshotSelectionState> redo_;
};

}  // desktop_orb::ui 命名空间结束
