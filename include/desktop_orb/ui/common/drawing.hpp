#pragma once

// 文件职责：提供 UI 层共用的 GDI 色彩、填充、文本和直线绘制函数。
// 本模块不拥有传入的 HDC；临时创建的画笔、字体和画刷由实现释放。
#include <windows.h>
#include <string>

namespace desktop_orb::ui {

// 作用：把 RGB 分量组合为 Win32 COLORREF。
// 参数：red、green、blue 分别为 0-255 的颜色分量。返回：组合后的颜色值。
[[nodiscard]] constexpr COLORREF Color(BYTE red, BYTE green, BYTE blue) {
    return RGB(red, green, blue);
}
// 作用：用纯色填充指定设备区域。
// 参数：dc 为目标 HDC；bounds 为填充矩形；color 为填充色。返回：无。
void Fill(HDC dc, RECT bounds, COLORREF color);
// 作用：按字体参数在矩形内绘制文本。
// 参数：dc 为目标 HDC；value 为文本；bounds 为区域；pixel_height 为字体像素高度；
//       color 为字色；weight 为字重；format 为 DrawText 布局标志。返回：无。
void Text(HDC dc, const std::wstring& value, RECT bounds, int pixel_height,
          COLORREF color, int weight = FW_NORMAL, UINT format = DT_SINGLELINE | DT_VCENTER);
// 作用：连接两个点绘制线段。
// 参数：dc 为目标 HDC；from、to 为端点；color 为线色；width 为线宽。返回：无。
void Line(HDC dc, POINT from, POINT to, COLORREF color, int width = 1);

}  // desktop_orb::ui 命名空间结束
