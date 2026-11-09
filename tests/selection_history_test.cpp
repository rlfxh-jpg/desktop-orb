// 文件职责：验证截图选区编辑历史的提交、撤销、重做和新分支规则。
// 测试只使用值状态，不创建窗口或访问用户桌面。
#include "desktop_orb/ui/screenshot/selection_history.hpp"

using desktop_orb::ui::ScreenshotSelectionHistory;
using desktop_orb::ui::ScreenshotSelectionState;

// 作用：验证画笔、马赛克和文字状态的撤销、重做及重做分支清除。
// 参数：无。返回：规则正确为 0；失败时返回对应非零步骤码。
int main() {
    ScreenshotSelectionHistory history;
    ScreenshotSelectionState original{{10, 20, 110, 120}, true, {}};
    ScreenshotSelectionState painted = original;
    painted.edits.strokes.push_back({{{5, 5}, {12, 12}}});
    if (!history.Commit(original, painted) || !history.can_undo()) return 1;
    if (history.Commit(painted, painted)) return 2;

    ScreenshotSelectionState restored;
    if (!history.Undo(painted, restored) || !restored.edits.strokes.empty() ||
        !history.can_redo()) return 3;
    if (!history.Redo(restored, restored) || restored.edits.strokes.size() != 1 ||
        restored.edits.strokes.front().points.size() != 2) return 4;

    if (!history.Undo(painted, restored)) return 5;
    ScreenshotSelectionState pixelated = original;
    pixelated.edits.pixelated_areas.push_back({2, 3, 20, 21});
    if (!history.Commit(restored, pixelated) || history.can_redo()) return 6;
    if (!history.Undo(pixelated, restored) || !restored.edits.pixelated_areas.empty()) return 7;
    if (!history.Redo(restored, restored) || restored.edits.pixelated_areas.size() != 1) return 8;
    ScreenshotSelectionState captioned = restored;
    captioned.edits.texts.push_back({{8, 9}, L"截图说明", 24});
    if (!history.Commit(restored, captioned)) return 9;
    if (!history.Undo(captioned, restored) || !restored.edits.texts.empty()) return 10;
    if (!history.Redo(restored, restored) || restored.edits.texts.size() != 1 ||
        restored.edits.texts.front().content != L"截图说明") return 11;
    history.Reset();
    return !history.can_undo() && !history.can_redo() ? 0 : 12;
}
