#pragma once

// 文件职责：声明覆盖虚拟桌面的截图选区窗口及其交互回调。
// 窗口只借用平台层快照位图；截图像素的采集、裁剪、保存由 platform/screenshot 负责。
#include "desktop_orb/platform/screenshot/screenshot_service.hpp"
#include "desktop_orb/ui/common/window_base.hpp"
#include <functional>

namespace desktop_orb::ui {

// 作用：表示选区完成后用户请求的操作。
// 参数：无。返回：无。
enum class ScreenshotAction { copy, save, recapture, close };

// 作用：覆盖所有显示器显示冻结画面、暗色遮罩和可拖动截图选区。
// 参数：无。返回：无；快照由调用方持有，窗口关闭前调用方需保证其有效。
class SelectionWindow final : public WindowBase {
public:
    // 作用：销毁选区后台绘图设备上下文和位图。参数：无。返回：无。
    ~SelectionWindow() override;
    // 作用：创建初始隐藏的截图选区窗口。参数：无。返回：创建成功为 true。
    bool Create();
    // 作用：显示快照并重置为等待鼠标框选状态。
    // 参数：snapshot 为仍有效的平台快照服务。返回：显示成功为 true；窗口定位失败时为 false。
    // 窗口显示后借用其位图进行绘制。
    [[nodiscard]] bool ShowFor(platform::ScreenshotService& snapshot);
    // 作用：隐藏选区窗口并清理借用的快照引用。参数：无。返回：无。
    void Hide();
    // 作用：查询选区界面是否显示。参数：无。返回：显示为 true。
    [[nodiscard]] bool visible() const { return visible_; }
    // 作用：通知应用层执行截图操作。参数：action 为请求操作；selection 为屏幕坐标选区。
    // 返回：无；空回调时不执行操作。
    std::function<void(ScreenshotAction, RECT)> on_action;

protected:
    // 作用：处理绘制、鼠标拖动、键盘取消及 DPI/显示器变化。
    // 参数：message 为消息码；wparam、lparam 为消息参数。返回：已处理消息为 0，否则交由基类。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    // 作用：把客户区点转换成虚拟桌面屏幕坐标。参数：point 为客户区坐标。
    // 返回：对应的屏幕坐标点。
    [[nodiscard]] POINT ScreenPoint(POINT point) const;
    // 作用：把鼠标拖动起止点规范化为屏幕坐标矩形。参数：无。返回：当前选区。
    [[nodiscard]] RECT Selection() const;
    // 作用：重新绘制冻结桌面、遮罩、选区和操作栏。参数：无。返回：无。
    void Paint();
    // 作用：确保指定尺寸的 32 位后台绘图表面存在。
    // 参数：width、height 为虚拟桌面像素尺寸。返回：资源准备成功为 true。
    bool EnsureFrameSurface(int width, int height);
    // 作用：把已完整绘制的后台表面一次性更新到分层窗口。
    // 参数：无。返回：Windows 接受图像更新为 true。
    bool PresentFrame();
    // 作用：在后台设备上下文绘制冻结桌面和所有选区 UI。
    // 参数：dc 为尺寸与虚拟桌面相同的目标设备上下文。返回：无。
    void DrawFrame(HDC dc);
    // 作用：根据选区尺寸计算工具栏屏幕矩形。参数：无。返回：屏幕坐标矩形。
    [[nodiscard]] RECT ToolbarBounds() const;
    // 作用：在指定操作栏局部点命中操作按钮。参数：point 为客户区坐标。
    // 返回：命中操作；未命中时为 close（调用方会单独验证操作栏范围）。
    [[nodiscard]] ScreenshotAction HitAction(POINT point) const;
    // 作用：根据动作通知应用层并保持窗口生命周期由应用管理。
    // 参数：action 为请求操作。返回：无。
    void Dispatch(ScreenshotAction action);

    platform::ScreenshotService* snapshot_ = nullptr;
    HDC frame_dc_ = nullptr;
    HBITMAP frame_bitmap_ = nullptr;
    HGDIOBJ previous_frame_bitmap_ = nullptr;
    int frame_width_ = 0;
    int frame_height_ = 0;
    POINT anchor_{};
    POINT cursor_{};
    bool dragging_ = false;
    bool selected_ = false;
    bool visible_ = false;
};

}  // desktop_orb::ui 命名空间结束
