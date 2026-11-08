// 文件职责：解析 Explorer 桌面图标宿主，并为围栏卡片安排不遮挡应用的 Z 序。
// 本模块只查询与调整窗口顺序；围栏窗口的创建、绘制和生命周期由 UI 模块负责。
#include "desktop_orb/platform/desktop_fence/desktop_window_layer.hpp"

#include <cwchar>

namespace desktop_orb::platform {
namespace {

// 作用：查找桌面图标列表所属的 Explorer 顶层窗口。
// 参数：无。返回：Progman 或承载桌面视图的 WorkerW；找不到时返回空句柄。
HWND FindDesktopHost() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    if (FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) return progman;

    struct SearchState {
        HWND host = nullptr;
        DWORD explorer_process = 0;
    } state;
    GetWindowThreadProcessId(progman, &state.explorer_process);

    // 核心逻辑：旧版 Windows 会把桌面图标视图从 Progman 移入 WorkerW，
    // 只接受同属 Explorer 且直接承载 SHELLDLL_DefView 的 WorkerW。
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto* search = reinterpret_cast<SearchState*>(parameter);
        if (!IsWindow(window)) return TRUE;
        wchar_t class_name[32]{};
        GetClassNameW(window, class_name, 32);
        if (std::wcscmp(class_name, L"WorkerW") != 0) return TRUE;
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process != search->explorer_process ||
            !FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr)) return TRUE;
        search->host = window;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&state));
    return state.host;
}

// 作用：判断顶层窗口当前是否能遮挡桌面内容。
// 参数：window 为待检查窗口。返回：可见、未最小化且有有效屏幕区域时为 true。
bool HasVisibleSurface(HWND window) {
    if (!IsWindowVisible(window) || IsIconic(window)) return false;
    RECT bounds{};
    return GetWindowRect(window, &bounds) && bounds.right > bounds.left &&
        bounds.bottom > bounds.top;
}

// 作用：查询窗口是否处于系统最顶层窗口带。
// 参数：window 为待检查窗口。返回：具有 WS_EX_TOPMOST 时为 true。
bool IsTopmost(HWND window) {
    return (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

}  // 匿名命名空间结束

// 作用：将围栏窗口放入桌面层并留在普通应用窗口后方。
// 参数：window 为待定位的围栏顶层窗口。返回：成功调整 Z 序为 true，否则为 false。
bool PlaceBehindApplications(HWND window) {
    if (!IsWindow(window)) return false;
    const HWND host = FindDesktopHost();
    if (!host) return false;

    HWND above = GetWindow(host, GW_HWNDPREV);
    while (above && above != window && !HasVisibleSurface(above))
        above = GetWindow(above, GW_HWNDPREV);

    // 核心逻辑：把卡片插入桌面宿主上方最低的可见窗口之后，应用窗口仍位于卡片前面。
    // 若宿主上方只有置顶窗口，则将卡片放在普通窗口带顶部，避免被置于桌面宿主后面。
    HWND insert_after = above;
    if (!above) insert_after = HWND_TOP;
    else if (IsTopmost(above) && !IsTopmost(host)) insert_after = HWND_TOP;
    return SetWindowPos(window, insert_after, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
}

}  // desktop_orb::platform 命名空间结束
