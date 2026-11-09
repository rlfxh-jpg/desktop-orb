#pragma once

// 文件职责：声明截图工具栏的内绘矢量图标接口。
// 本模块只借用目标 HDC，不加载图片资源，也不持有 GDI 对象。
#include <windows.h>

namespace desktop_orb::ui {

enum class ScreenshotAction;

// 作用：在按钮中央绘制对应截图功能的简洁线框图标。
// 参数：dc 为目标设备上下文；action 为截图功能；bounds 为图标绘制区域；color 为线条颜色。
// 返回：绘制成功为 true；设备上下文、区域或 GDI 资源无效时为 false。
bool DrawScreenshotToolbarIcon(HDC dc, ScreenshotAction action, RECT bounds, COLORREF color);

}  // desktop_orb::ui 命名空间结束
