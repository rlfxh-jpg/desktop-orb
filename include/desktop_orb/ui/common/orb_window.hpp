#pragma once

// 文件职责：管理悬浮球原生窗口、拖动吸边、透明度和输入事件。
// 图标绘制委托 OrbArtwork，业务状态转换由 App 回调处理。
#include "desktop_orb/ui/common/orb_artwork.hpp"
#include "desktop_orb/ui/common/window_base.hpp"
#include <functional>

namespace desktop_orb::ui {

class OrbWindow final : public WindowBase {
public:
    // 事件回调由 App 安装；参数与返回值依各 std::function 签名。
    std::function<void()> on_click;
    std::function<void()> on_drag_start;
    std::function<void(POINT)> on_moved;
    std::function<void()> on_tick;
    std::function<void()> on_hotkey;
    std::function<void()> on_context;
    std::function<void()> on_clipboard_changed;

    // 作用：创建并显示置顶悬浮球。
    // 参数：initial 为初始屏幕坐标。返回：窗口和图片创建成功为 true，否则为 false。
    bool Create(POINT initial);
    // 作用：更新整体透明度；图片自身 Alpha 保持不变。
    // 参数：alpha 为 0-255 的整体透明度。返回：无；绘制失败时保留旧值。
    void SetAlpha(BYTE alpha);
    // 作用：将悬浮球吸附到所在显示器最近的左右边缘。参数：无。返回：无。
    void SnapToEdge();
    // 作用：读取悬浮球当前屏幕矩形。参数：无。返回：Win32 屏幕坐标矩形。
    [[nodiscard]] RECT Bounds() const;

protected:
    // 作用：处理绘制、鼠标、快捷键、计时器及 DPI 等窗口消息。
    // 参数：message 为消息码；wparam、lparam 为对应消息参数。
    // 返回：已处理消息返回 0，其余消息返回基类处理结果。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    OrbArtwork artwork_;
    POINT drag_cursor_{};
    RECT drag_origin_{};
    bool dragging_ = false;
    bool moved_ = false;
    BYTE alpha_ = 0;
};

}  // desktop_orb::ui 命名空间结束
