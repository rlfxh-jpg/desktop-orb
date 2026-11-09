// 文件职责：实现截图选区和标注的值状态比较、撤销及重做。
// 历史上限固定为 40 步；应用窗口负责在新截图开始或关闭时重置历史。
#include "desktop_orb/ui/screenshot/selection_history.hpp"

#include <utility>

namespace desktop_orb::ui {
namespace {

constexpr size_t kMaximumHistorySteps = 40;

// 作用：比较两个 Win32 矩形的四个边界。
// 参数：left、right 为待比较矩形。返回：边界完全一致为 true。
bool SameRect(RECT left, RECT right) {
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

// 作用：比较两份截图编辑内容，避免无变化的鼠标动作占用撤销步骤。
// 参数：left、right 为待比较的标注数据。返回：笔迹、马赛克和文字都相同为 true。
bool SameEdits(const platform::ScreenshotEdits& left, const platform::ScreenshotEdits& right) {
    if (left.strokes.size() != right.strokes.size() ||
        left.pixelated_areas.size() != right.pixelated_areas.size() ||
        left.texts.size() != right.texts.size()) return false;
    for (size_t index = 0; index < left.strokes.size(); ++index) {
        const auto& a = left.strokes[index].points;
        const auto& b = right.strokes[index].points;
        if (a.size() != b.size()) return false;
        for (size_t point = 0; point < a.size(); ++point)
            if (a[point].x != b[point].x || a[point].y != b[point].y) return false;
    }
    for (size_t index = 0; index < left.pixelated_areas.size(); ++index)
        if (!SameRect(left.pixelated_areas[index], right.pixelated_areas[index])) return false;
    for (size_t index = 0; index < left.texts.size(); ++index) {
        const auto& a = left.texts[index];
        const auto& b = right.texts[index];
        if (a.origin.x != b.origin.x || a.origin.y != b.origin.y ||
            a.font_height != b.font_height || a.content != b.content) return false;
    }
    return true;
}

// 作用：判断选区及编辑状态是否完全相同。
// 参数：left、right 为两个状态。返回：内容相同时为 true。
bool SameState(const ScreenshotSelectionState& left, const ScreenshotSelectionState& right) {
    return left.selected == right.selected &&
        SameRect(left.selection, right.selection) && SameEdits(left.edits, right.edits);
}

}  // 匿名命名空间结束

// 作用：开始新的截图历史，释放之前的状态副本。
// 参数：无。返回：无。
void ScreenshotSelectionHistory::Reset() {
    undo_.clear();
    redo_.clear();
}

// 作用：提交发生实际变化的状态，并限制撤销栈长度。
// 参数：before 为操作前状态；after 为操作后状态。返回：记录新步骤为 true。
bool ScreenshotSelectionHistory::Commit(const ScreenshotSelectionState& before,
    const ScreenshotSelectionState& after) {
    if (SameState(before, after)) return false;
    undo_.push_back(before);
    if (undo_.size() > kMaximumHistorySteps) undo_.erase(undo_.begin());
    // 核心逻辑：新分支出现后，旧分支中的重做状态已不再对应当前截图。
    redo_.clear();
    return true;
}

// 作用：退回最近一次操作，并保留当前状态以供重做。
// 参数：current 为当前状态；restored 接收上一状态。返回：成功退回为 true。
bool ScreenshotSelectionHistory::Undo(const ScreenshotSelectionState& current,
    ScreenshotSelectionState& restored) {
    if (undo_.empty()) return false;
    redo_.push_back(current);
    restored = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

// 作用：恢复最近一次被撤销的操作，并重新加入撤销栈。
// 参数：current 为当前状态；restored 接收下一状态。返回：成功恢复为 true。
bool ScreenshotSelectionHistory::Redo(const ScreenshotSelectionState& current,
    ScreenshotSelectionState& restored) {
    if (redo_.empty()) return false;
    undo_.push_back(current);
    if (undo_.size() > kMaximumHistorySteps) undo_.erase(undo_.begin());
    restored = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

}  // desktop_orb::ui 命名空间结束
