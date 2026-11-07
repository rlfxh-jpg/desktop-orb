#include "desktop_orb/ui/drawing.hpp"

namespace desktop_orb::ui {

void Fill(HDC dc, RECT bounds, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

void Text(HDC dc, const std::wstring& value, RECT bounds, int pixel_height,
          COLORREF color, int weight, UINT format) {
    HFONT font = CreateFontW(-pixel_height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &bounds, format);
    SelectObject(dc, old_font);
    DeleteObject(font);
}

void Line(HDC dc, POINT from, POINT to, COLORREF color, int width) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, from.x, from.y, nullptr);
    LineTo(dc, to.x, to.y);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

}  // namespace desktop_orb::ui
