#pragma once

// 文件职责：提供桌面围栏读取显示器壁纸路径的 Windows 平台接口。
// 本模块只负责匹配显示器并读取系统壁纸服务，不解码图片或参与界面绘制。
#include <windows.h>
#include <string>

namespace desktop_orb::platform {

class DesktopWallpaper final {
public:
    // 作用：读取覆盖指定显示器的壁纸文件路径。
    // 参数：monitor_bounds 为虚拟桌面坐标中的显示器边界。
    // 返回：匹配到的壁纸文件路径；COM 服务不可用或显示器未匹配时返回空字符串。
    [[nodiscard]] static std::wstring PathForMonitor(RECT monitor_bounds);
};

}  // desktop_orb::platform 命名空间结束
