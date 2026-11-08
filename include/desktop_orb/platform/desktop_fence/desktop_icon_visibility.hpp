#pragma once

// 文件职责：记录并切换 Windows 桌面图标视图的显示状态。
// 本类只持有 Explorer 桌面视图 HWND，不拥有该窗口；恢复时按隐藏前状态还原。
#include <windows.h>
#include <vector>

namespace desktop_orb::platform {

class DesktopIconVisibility final {
public:
    // 作用：恢复此前隐藏的桌面视图并释放状态记录。参数：无。返回：无。
    ~DesktopIconVisibility();

    // 作用：隐藏当前可见的 Explorer 桌面图标视图。
    // 参数：无。返回：找到并隐藏至少一个桌面视图为 true；没有可隐藏视图时为 false。
    bool Hide();

    // 作用：按 Hide 调用前的可见状态恢复桌面视图。
    // 参数：无。返回：无；Explorer 已重启或句柄失效时跳过该视图。
    void Restore();

private:
    struct ViewState {
        HWND window = nullptr;
        bool was_visible = false;
    };

    std::vector<ViewState> views_;
};

}  // desktop_orb::platform 命名空间结束
