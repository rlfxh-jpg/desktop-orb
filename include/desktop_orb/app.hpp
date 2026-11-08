#pragma once

// 文件职责：协调悬浮球、环绕菜单和窗口切换面板的状态与生命周期。
// 平台枚举、图片解码和具体绘制由各自模块负责。
#include "desktop_orb/platform/common/settings.hpp"
#include "desktop_orb/platform/screenshot/screenshot_ai_client.hpp"
#include "desktop_orb/platform/screenshot/screenshot_service.hpp"
#include "desktop_orb/platform/window_switcher/window_catalog.hpp"
#include "desktop_orb/ui/common/feature_menu.hpp"
#include "desktop_orb/ui/desktop_fence/fence_panel.hpp"
#include "desktop_orb/ui/common/orb_window.hpp"
#include "desktop_orb/ui/screenshot/selection_window.hpp"
#include "desktop_orb/ui/window_switcher/switcher_panel.hpp"
#include <future>

namespace desktop_orb {

class App {
public:
    // 作用：构造应用并把窗口切换面板关联到窗口目录。参数：无。返回：无。
    App();
    // 作用：注销已注册的全局快捷键。参数：无。返回：无。
    ~App();
    // 作用：加载设置、创建界面和注册回调及快捷键。参数：无。
    // 返回：初始化完成为 true；必要窗口或菜单创建失败为 false。
    bool Initialize();
    // 作用：运行 Win32 消息循环。参数：无。返回：退出消息携带的进程退出码。
    int Run();

private:
    // 作用：定时更新前台窗口记录、悬停透明度及面板外点击状态。参数：无。返回：无。
    void Tick();
    // 作用：切换环绕菜单显示状态，打开时先关闭预览面板。参数：无。返回：无。
    void ToggleMenu();
    // 作用：在悬浮球所在显示器的同侧打开窗口切换面板。参数：无。返回：无。
    void OpenSwitcher();
    // 作用：在悬浮球所在显示器打开桌面围栏项目面板。
    // 参数：无。返回：无；打开面板时隐藏悬浮球，关闭后由回调恢复。
    void OpenDesktopFence();
    // 作用：暂时隐藏悬浮球，捕获虚拟桌面并打开截图选区界面。
    // 参数：无。返回：无；采集或界面创建失败时恢复悬浮球并显示错误信息。
    void CaptureScreenshot();
    // 作用：处理选区窗口提交的文字提取、截图总结、复制、保存、重新截取或关闭操作。
    // 参数：action 为用户操作；selection 为屏幕坐标选区。返回：无。
    void HandleScreenshotAction(ui::ScreenshotAction action, RECT selection);
    // 作用：在后台启动一次截图文字提取或内容总结。
    // 参数：selection 为屏幕坐标选区；task 为分析类型。返回：无；失败时显示错误并保留选区。
    void StartScreenshotAnalysis(RECT selection, platform::ScreenshotAiTask task);
    // 作用：轮询后台分析并在 UI 线程处理完成结果。参数：无。返回：无。
    void PollScreenshotAnalysis();
    // 作用：关闭菜单和预览面板并恢复悬浮球。参数：无。返回：无。
    void CloseOverlays();
    // 作用：显示设置与退出命令的右键菜单。参数：无。返回：无。
    void ShowContextMenu();
    // 作用：重新读取快捷键设置，注册失败时尝试恢复旧快捷键。参数：无。返回：无。
    void ReloadSettings();
    // 作用：记录并持久化悬浮球位置。参数：position 为吸边后的屏幕坐标。返回：无。
    void SavePosition(POINT position);
    // 作用：向 Windows 注册全局快捷键。参数：hotkey 为已解析的修饰键和主键。
    // 返回：注册成功为 true；被占用或系统拒绝时为 false。
    bool RegisterShortcut(const platform::Hotkey& hotkey);

    platform::SettingsStore settings_store_;
    platform::Settings settings_;
    platform::ScreenshotAiConfig screenshot_ai_config_;
    platform::WindowCatalog catalog_;
    platform::ScreenshotService screenshot_service_;
    ui::OrbWindow orb_;
    ui::FeatureMenu menu_;
    ui::SwitcherPanel panel_;
    ui::FencePanel fence_panel_;
    ui::SelectionWindow screenshot_window_;
    std::future<platform::ScreenshotAiResult> screenshot_ai_future_;
    platform::ScreenshotAiTask screenshot_ai_task_ = platform::ScreenshotAiTask::extract_text;
    bool screenshot_ai_pending_ = false;
    bool hotkey_registered_ = false;
    bool previous_left_down_ = false;
    bool previous_escape_down_ = false;
};

}  // desktop_orb 命名空间结束
