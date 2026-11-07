#pragma once

// 文件职责：声明 Windows 桌面截图服务及其执行结果。
// 本模块负责捕获虚拟桌面、保存 PNG 和发布剪贴板位图，不负责界面提示或悬浮球状态。
#include <filesystem>
#include <string>

namespace desktop_orb::platform {

// 作用：承载截图操作的文件保存状态、剪贴板状态和失败原因。
// 参数：无。返回：无；saved 为 false 时 error 描述主要失败原因。
struct ScreenshotResult {
    bool saved = false;
    bool copied_to_clipboard = false;
    std::filesystem::path file_path;
    std::wstring error;
};

// 作用：提供虚拟桌面截图、PNG 保存与剪贴板复制能力。
// 参数：无。返回：无；对象不持有跨调用的系统资源。
class ScreenshotService final {
public:
    // 作用：捕获所有显示器拼接成的虚拟桌面，并保存到用户图片目录。
    // 参数：无。返回：保存状态、剪贴板状态及 PNG 文件路径；采集或保存失败时包含错误原因。
    [[nodiscard]] ScreenshotResult CaptureAllDisplays() const;
};

}  // desktop_orb::platform 命名空间结束
