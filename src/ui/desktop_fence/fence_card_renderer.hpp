#pragma once

// 文件职责：声明桌面围栏卡片的自绘数据接口。
// 绘制器只读取卡片状态并绘制到传入的 HDC，不拥有窗口、图标或项目数据。
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <optional>
#include <vector>

#include "desktop_orb/platform/desktop_fence/desktop_item_catalog.hpp"

namespace desktop_orb::ui {

// 作用：启动围栏绘图使用的进程级 GDI+ 运行时。
// 参数：无。返回：启动成功为 true；失败后壁纸解码和绘制均使用纯色背景降级。
[[nodiscard]] bool InitializeFenceRenderer();

// 作用：把目标显示器壁纸裁成卡片背景并叠加浅色玻璃表面。
// 参数：dc 为目标设备上下文；window 为卡片窗口；client 为客户区；dpi 为显示器 DPI；
//       monitor_bounds 为壁纸所属显示器边界；wallpaper 为已解码壁纸，允许为空。
// 返回：无；壁纸绘制失败时显示不透明浅色底板，不暴露后方应用内容。
void DrawFenceBackdrop(HDC dc, HWND window, RECT client, UINT dpi, RECT monitor_bounds,
                       Gdiplus::Bitmap* wallpaper);

// 作用：绘制围栏分类标题、均分列宽且左对齐的图标网格、项目状态和覆盖式滚动条。
// 参数：dc 为目标设备上下文；client 为客户区；dpi 为卡片显示器 DPI；category 为分类；
//       category_icon 为借用标题图标；items 与 icons 为同序项目数据和本卡片拥有的图标；
//       selected、hovered 为选中和悬停索引；scroll_row 为首个可见行。
// 返回：无；只读取传入数据，不接管 HICON、字体或其他 GDI 资源所有权。
void DrawFenceContents(HDC dc, RECT client, UINT dpi,
    platform::DesktopFenceCategory category, HICON category_icon,
    const std::vector<platform::DesktopItem>& items, const std::vector<HICON>& icons,
    const std::optional<size_t>& selected, const std::optional<size_t>& hovered,
    int scroll_row);

}  // desktop_orb::ui 命名空间结束
