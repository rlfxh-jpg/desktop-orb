// 文件职责：验证剪贴板历史直接接收截图位图时的去重、置顶与顺序规则。
// 测试不读取或改写系统剪贴板，只构造临时 GDI 位图并由测试释放。
#include "desktop_orb/platform/clipboard/clipboard_history.hpp"

#include <windows.h>

using desktop_orb::platform::ClipboardHistory;

// 作用：构造一张填充指定颜色的独立小位图。
// 参数：color 为填充颜色。返回：创建成功的 HBITMAP；失败时为空。
HBITMAP CreateTestBitmap(COLORREF color) {
    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 4, 4);
    HDC memory = bitmap ? CreateCompatibleDC(screen) : nullptr;
    if (memory) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        HBRUSH brush = CreateSolidBrush(color);
        RECT area{0, 0, 4, 4};
        FillRect(memory, &area, brush);
        DeleteObject(brush);
        SelectObject(memory, previous);
        DeleteDC(memory);
    } else if (bitmap) {
        DeleteObject(bitmap);
        bitmap = nullptr;
    }
    ReleaseDC(nullptr, screen);
    return bitmap;
}

// 作用：验证直接保存位图与历史排序，不依赖全局剪贴板状态。
// 参数：无。返回：全部规则满足为 0，失败时返回对应非零步骤码。
int main() {
    ClipboardHistory history;
    HBITMAP first = CreateTestBitmap(RGB(255, 0, 0));
    HBITMAP second = CreateTestBitmap(RGB(0, 0, 255));
    if (!first || !second) return 1;
    const bool added = history.AddBitmap(first) && history.AddBitmap(second);
    if (!added) return 2;
    auto entries = history.Entries();
    if (entries.size() != 2 || entries[0]->width != 4 || entries[0]->height != 4) return 3;
    const auto first_id = entries[1]->id;
    if (!history.TogglePin(first_id)) return 4;
    entries = history.Entries();
    if (entries.front()->id != first_id || !entries.front()->pinned) return 5;
    if (!history.AddBitmap(first) || history.Entries().size() != 2) return 6;
    DeleteObject(first);
    DeleteObject(second);
    return 0;
}
