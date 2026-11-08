// 文件职责：通过 IDesktopWallpaper 读取与显示器边界匹配的壁纸文件。
// COM 接口由当前调用线程临时持有；Shell 返回的字符串在复制后立即释放。
#include "desktop_orb/platform/desktop_fence/desktop_wallpaper.hpp"

#include <shobjidl.h>
#include <wrl/client.h>

namespace desktop_orb::platform {

std::wstring DesktopWallpaper::PathForMonitor(RECT monitor_bounds) {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return {};

    std::wstring result;
    Microsoft::WRL::ComPtr<IDesktopWallpaper> wallpaper;
    if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(wallpaper.GetAddressOf())))) {
        UINT count = 0;
        if (SUCCEEDED(wallpaper->GetMonitorDevicePathCount(&count))) {
            for (UINT index = 0; index < count && result.empty(); ++index) {
                PWSTR monitor_id = nullptr;
                if (FAILED(wallpaper->GetMonitorDevicePathAt(index, &monitor_id)) || !monitor_id)
                    continue;

                RECT bounds{};
                const bool matches = SUCCEEDED(wallpaper->GetMonitorRECT(monitor_id, &bounds)) &&
                    bounds.left == monitor_bounds.left && bounds.top == monitor_bounds.top &&
                    bounds.right == monitor_bounds.right && bounds.bottom == monitor_bounds.bottom;
                PWSTR wallpaper_path = nullptr;
                const HRESULT path_result = matches
                    ? wallpaper->GetWallpaper(monitor_id, &wallpaper_path) : E_FAIL;
                CoTaskMemFree(monitor_id);

                if (SUCCEEDED(path_result) && wallpaper_path) result = wallpaper_path;
                if (wallpaper_path) CoTaskMemFree(wallpaper_path);
            }
        }
    }

    // 核心约束：RPC_E_CHANGED_MODE 表示线程已由宿主初始化为另一种 COM apartment，
    // 此时继续使用该 apartment，但不能调用 CoUninitialize 平衡并非本函数创建的引用。
    if (SUCCEEDED(apartment)) CoUninitialize();
    return result;
}

}  // desktop_orb::platform 命名空间结束
