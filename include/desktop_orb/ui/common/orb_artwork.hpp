#pragma once

// 文件职责：将悬浮球图标合成到 Win32 分层窗口；PNG 加载由 IconImage 负责。
// 本模块只拥有图标对象，不拥有传入的 HWND。
#include "desktop_orb/ui/common/icon_image.hpp"
#include <windows.h>

namespace desktop_orb::ui {

class OrbArtwork final {
public:
    // 作用：确认悬浮球图片可用，供窗口创建前检查。
    // 参数：无。返回：图片有效为 true，缺失或解码失败为 false。
    [[nodiscard]] bool ready() const { return image_.ready(); }
    // 作用：按窗口当前位置和尺寸更新悬浮球画面。
    // 参数：window 为已创建的分层窗口；opacity 为叠加在 PNG Alpha 上的整体透明度。
    // 返回：更新成功为 true；窗口、图标或 Win32 绘制资源不可用时为 false。
    bool Render(HWND window, BYTE opacity) const;

private:
    IconImage image_{L"orb.png"};
};

}  // desktop_orb::ui 命名空间结束
