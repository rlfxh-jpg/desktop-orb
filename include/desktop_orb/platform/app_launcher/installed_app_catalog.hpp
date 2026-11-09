#pragma once

// 文件职责：声明 Windows AppsFolder 中可启动应用的查询、图标和启动接口。
// 本模块只借用 Shell 命名空间，返回的图标句柄由调用方释放。
#include <string>
#include <vector>
#include <windows.h>

namespace desktop_orb::platform {

// AppsFolder 不提供可靠的“系统预装”标记，因此按启动条目的封装形式分组。
enum class AppKind { desktop, packaged };

struct InstalledApp {
    std::wstring name;
    std::wstring parsing_name;
    AppKind kind = AppKind::desktop;
};

class InstalledAppCatalog final {
public:
    // 作用：列出 Windows AppsFolder 中当前用户可启动的应用。
    // 参数：error 接收系统查询失败时的说明。返回：按名称排序的应用；部分条目失败时跳过该条目。
    [[nodiscard]] std::vector<InstalledApp> List(std::wstring& error) const;

    // 作用：向 Windows Shell 请求启动指定应用。
    // 参数：app 为列表项。返回：Shell 接受启动请求为 true，解析或启动失败为 false。
    [[nodiscard]] static bool Launch(const InstalledApp& app);

    // 作用：读取应用在 Shell 中的图标。
    // 参数：app 为列表项。返回：新建的 HICON 或空句柄；非空句柄由调用方 DestroyIcon。
    [[nodiscard]] static HICON LoadIcon(const InstalledApp& app);
};

}  // desktop_orb::platform 命名空间结束
