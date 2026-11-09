#pragma once

// 文件职责：声明独立的桌面截图贴图窗口。
// 窗口独占裁剪后的 HBITMAP；关闭时释放位图，应用层负责对象生命周期。
#include "desktop_orb/ui/common/window_base.hpp"

namespace desktop_orb::ui {

class PinnedImageWindow final : public WindowBase {
public:
    // 作用：释放窗口及其独占的截图位图。参数：无。返回：无。
    ~PinnedImageWindow() override;
    // 作用：创建可拖动、置顶且不显示任务栏按钮的贴图窗口。
    // 参数：bitmap 为转移所有权的位图；position 为初始屏幕坐标。
    // 返回：创建成功为 true；失败时仍由本对象释放位图。
    bool Create(HBITMAP bitmap, POINT position);
    // 作用：报告用户是否通过右键关闭了贴图。参数：无。返回：关闭后为 true。
    [[nodiscard]] bool closed() const { return closed_; }

protected:
    // 作用：处理绘制、拖动和右键关闭。
    // 参数：message 为消息码；wparam、lparam 为消息参数。
    // 返回：已处理消息为 0 或命中测试结果，其他消息交由基类。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    HBITMAP bitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool closed_ = false;
};

}  // desktop_orb::ui 命名空间结束
