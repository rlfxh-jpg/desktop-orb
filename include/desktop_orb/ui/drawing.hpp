#pragma once

#include <windows.h>
#include <string>

namespace desktop_orb::ui {

[[nodiscard]] constexpr COLORREF Color(BYTE red, BYTE green, BYTE blue) {
    return RGB(red, green, blue);
}
void Fill(HDC dc, RECT bounds, COLORREF color);
void Text(HDC dc, const std::wstring& value, RECT bounds, int pixel_height,
          COLORREF color, int weight = FW_NORMAL, UINT format = DT_SINGLELINE | DT_VCENTER);
void Line(HDC dc, POINT from, POINT to, COLORREF color, int width = 1);

}  // namespace desktop_orb::ui
