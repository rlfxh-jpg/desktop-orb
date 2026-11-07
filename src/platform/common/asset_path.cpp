// 文件职责：实现 Windows 平台的资源路径定位；只负责路径，不负责文件读取。
// 依赖 GetModuleFileNameW 获取当前 exe 所在位置，供 UI 层加载图片。
#include "desktop_orb/platform/common/asset_path.hpp"

#include <windows.h>
#include <string>

namespace desktop_orb::platform {

std::filesystem::path AssetPath(std::wstring_view relative_path) {
    // 核心逻辑：安装路径可能超过 MAX_PATH；逐步扩容，避免把截断路径当作有效路径。
    std::wstring executable(MAX_PATH, L'\0');
    DWORD length = 0;
    for (;;) {
        length = GetModuleFileNameW(nullptr, executable.data(),
                                    static_cast<DWORD>(executable.size()));
        if (!length) return {};
        if (length < executable.size()) break;
        executable.resize(executable.size() * 2);
    }
    executable.resize(length);
    // 核心逻辑：快捷方式或其他程序启动时，工作目录可能与 exe 目录不同。
    return std::filesystem::path(executable).parent_path() / L"assets" / relative_path;
}

}  // desktop_orb::platform 命名空间结束
