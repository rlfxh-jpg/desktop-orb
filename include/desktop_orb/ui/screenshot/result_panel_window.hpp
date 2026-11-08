#pragma once

// 文件职责：声明截图分析结果的独立置顶窗口，提供可选择、可滚动的只读文本和复制命令。
// 该窗口由截图选区窗口拥有，但不作为分层截图窗口的子控件，避免整屏位图刷新覆盖结果文字。
#include "desktop_orb/ui/common/window_base.hpp"
#include <functional>
#include <string>

namespace desktop_orb::ui {

class ResultPanelWindow final : public WindowBase {
public:
    // 作用：创建初始隐藏的分析结果窗口及内部文本、复制控件。
    // 参数：owner 为全屏截图选区窗口句柄。返回：窗口与子控件全部创建成功为 true。
    bool Create(HWND owner);
    // 作用：设置结果标题和完整文本，并更新窗口内容。
    // 参数：title 为面板标题；text 为可选中和复制的分析结果。返回：无。
    void SetContent(std::wstring title, std::wstring text);
    // 作用：在给定屏幕位置显示结果窗口，且不抢占当前键盘焦点。
    // 参数：bounds 为屏幕坐标矩形。返回：无；窗口定位失败时保持隐藏。
    void ShowAt(RECT bounds);
    // 作用：隐藏结果面板。参数：无。返回：无。
    void Hide();
    // 作用：查询当前完整结果文本，供复制操作使用。
    // 参数：无。返回：当前文本的只读引用；没有结果时为空字符串。
    [[nodiscard]] const std::wstring& content() const { return content_; }

    // 作用：通知拥有者用户点击了复制按钮。参数：无。返回：无。
    std::function<void()> on_copy;

protected:
    // 作用：绘制面板背景和标题，并处理复制按钮命令。
    // 参数：message 为消息码；wparam、lparam 为消息参数。返回：已处理消息为 0，否则交由基类。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    // 作用：更新编辑框和复制按钮在面板客户区内的位置。
    // 参数：width、height 为面板客户区像素尺寸；dpi 为当前窗口 DPI。返回：无。
    void LayoutControls(int width, int height, UINT dpi);

    HWND edit_ = nullptr;
    HWND copy_button_ = nullptr;
    std::wstring title_;
    std::wstring content_;
};

}  // desktop_orb::ui 命名空间结束
