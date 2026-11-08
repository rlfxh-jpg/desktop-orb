// 文件职责：绘制桌面围栏标题、Shell 项目图标、标签、选择状态和滚动条。
// 本模块只绘制已准备好的只读数据，不负责窗口消息、项目枚举、图标加载或壁纸处理。
#include "fence_card_renderer.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>

namespace desktop_orb::ui {
namespace {

constexpr int kPadding = 8;
constexpr int kHeaderHeight = 56;
constexpr int kCellWidth = 68;
constexpr int kCellHeight = 88;
constexpr int kIconSize = 48;
constexpr COLORREF kText = RGB(35, 43, 57);
constexpr COLORREF kDivider = RGB(205, 212, 221);
constexpr COLORREF kSelected = RGB(219, 231, 246);
constexpr COLORREF kHovered = RGB(232, 238, 245);
constexpr COLORREF kTrack = RGB(228, 232, 237);
constexpr COLORREF kThumb = RGB(132, 140, 150);

// 作用：计算内容区域当前宽度可容纳的图标列数。
// 参数：client 为卡片客户区；dpi 为显示器 DPI。返回：至少为一列的图标列数。
int ColumnCount(RECT client, UINT dpi) {
    const int cell_width = platform::Scale(kCellWidth, dpi);
    const int usable = std::max(cell_width,
        static_cast<int>(client.right) - platform::Scale(kPadding * 2 + 12, dpi));
    return std::max(1, usable / cell_width);
}

}  // 匿名命名空间结束

void DrawFenceContents(HDC dc, RECT client, UINT dpi,
    platform::DesktopFenceCategory category, HICON category_icon,
    const std::vector<platform::DesktopItem>& items, const std::vector<HICON>& icons,
    const std::optional<size_t>& selected, const std::optional<size_t>& hovered,
    int scroll_row) {
    const int padding = platform::Scale(kPadding, dpi);
    const int header_height = platform::Scale(kHeaderHeight, dpi);
    const int cell_width = platform::Scale(kCellWidth, dpi);
    const int cell_height = platform::Scale(kCellHeight, dpi);
    const int icon_size = platform::Scale(kIconSize, dpi);
    const int content_top = platform::Scale(kHeaderHeight + 10, dpi);
    const int content_bottom = client.bottom - padding;
    const int columns = ColumnCount(client, dpi);
    const int visible_rows = std::max(1, (content_bottom - content_top) / cell_height);
    const int total_rows = static_cast<int>((items.size() + columns - 1) / columns);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kText);
    if (category_icon) {
        DrawIconEx(dc, padding + platform::Scale(8, dpi), platform::Scale(20, dpi),
            category_icon, platform::Scale(20, dpi), platform::Scale(20, dpi),
            0, nullptr, DI_NORMAL);
    }

    HFONT title_font = CreateFontW(platform::Scale(16, dpi), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, title_font);
    RECT title{padding + platform::Scale(44, dpi), 0, client.right - padding, header_height};
    const std::wstring heading = std::wstring(platform::DesktopItemCatalog::CategoryName(category)) +
        L"  " + std::to_wstring(items.size());
    DrawTextW(dc, heading.c_str(), -1, &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old_font);
    DeleteObject(title_font);

    HPEN divider = CreatePen(PS_SOLID, 1, kDivider);
    HGDIOBJ old_pen = SelectObject(dc, divider);
    MoveToEx(dc, padding, header_height + platform::Scale(8, dpi), nullptr);
    LineTo(dc, client.right - padding, header_height + platform::Scale(8, dpi));
    SelectObject(dc, old_pen);
    DeleteObject(divider);

    if (items.empty()) {
        RECT empty{padding, content_top, client.right - padding, content_bottom};
        SetTextColor(dc, RGB(105, 114, 126));
        DrawTextW(dc, L"此分类没有桌面项目", -1, &empty,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        HFONT item_font = CreateFontW(platform::Scale(12, dpi), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        old_font = SelectObject(dc, item_font);
        for (int row = 0; row < visible_rows; ++row) {
            const size_t row_start = static_cast<size_t>(scroll_row + row) * columns;
            if (row_start >= items.size()) break;
            const size_t row_count = std::min(static_cast<size_t>(columns), items.size() - row_start);
            const int row_left = padding;
            for (size_t column = 0; column < row_count; ++column) {
                const size_t index = row_start + column;
                const int left = row_left + static_cast<int>(column) * cell_width;
                const int top = content_top + row * cell_height;
                RECT cell{left + platform::Scale(2, dpi), top + platform::Scale(2, dpi),
                    left + cell_width - platform::Scale(2, dpi),
                    top + cell_height - platform::Scale(2, dpi)};
                if (selected == index || hovered == index) {
                    HBRUSH state = CreateSolidBrush(selected == index ? kSelected : kHovered);
                    FillRect(dc, &cell, state);
                    DeleteObject(state);
                }
                const int icon_x = left + (cell_width - icon_size) / 2;
                const int icon_y = top + platform::Scale(8, dpi);
                if (index < icons.size() && icons[index])
                    DrawIconEx(dc, icon_x, icon_y, icons[index], icon_size, icon_size,
                               0, nullptr, DI_NORMAL);
                RECT label{left + platform::Scale(3, dpi), icon_y + icon_size + platform::Scale(4, dpi),
                    left + cell_width - platform::Scale(3, dpi), top + cell_height - platform::Scale(2, dpi)};
                SetTextColor(dc, kText);
                DrawTextW(dc, items[index].name.c_str(), -1, &label,
                    DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        SelectObject(dc, old_font);
        DeleteObject(item_font);
    }

    if (total_rows > visible_rows) {
        const int track_x = client.right - padding;
        const int track_height = std::max(1, content_bottom - content_top);
        const int thumb_height = std::max(platform::Scale(22, dpi), track_height * visible_rows / total_rows);
        const int max_scroll = std::max(1, total_rows - visible_rows);
        const int thumb_top = content_top + (track_height - thumb_height) * scroll_row / max_scroll;
        HBRUSH track = CreateSolidBrush(kTrack);
        HBRUSH thumb = CreateSolidBrush(kThumb);
        RECT track_rect{track_x, content_top, track_x + platform::Scale(3, dpi), content_bottom};
        RECT thumb_rect{track_x, thumb_top, track_x + platform::Scale(3, dpi), thumb_top + thumb_height};
        FillRect(dc, &track_rect, track);
        FillRect(dc, &thumb_rect, thumb);
        DeleteObject(track);
        DeleteObject(thumb);
    }
}

}  // desktop_orb::ui 命名空间结束
