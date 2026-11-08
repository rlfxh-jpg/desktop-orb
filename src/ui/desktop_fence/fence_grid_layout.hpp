#pragma once

// 文件职责：统一计算围栏项目网格的列数、均分边界和左右留白。
// 本模块不绘图、不处理窗口消息；绘制与鼠标命中共用相同的像素边界。
#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>

namespace desktop_orb::ui {

struct FenceGridLayout {
    int left = 0;
    int width = 0;
    int columns = 1;

    // 作用：取得指定列在客户区中的左边界。
    // 参数：column 为从零开始的列索引。返回：DPI 换算后的像素坐标。
    [[nodiscard]] int CellLeft(int column) const {
        return left + width * column / columns;
    }

    // 作用：取得指定列在客户区中的右边界。
    // 参数：column 为从零开始的列索引。返回：DPI 换算后的像素坐标。
    [[nodiscard]] int CellRight(int column) const {
        return left + width * (column + 1) / columns;
    }

    // 作用：将客户区横坐标映射为网格列。
    // 参数：x 为鼠标横坐标。返回：有效列索引；左右留白处返回 -1。
    [[nodiscard]] int ColumnAt(int x) const {
        if (x < left || x >= left + width) return -1;
        return std::min(columns - 1, (x - left) * columns / width);
    }
};

// 作用：根据卡片宽度和 DPI 生成左右等距、列宽均分的项目网格。
// 参数：client_width 为客户区像素宽度；dpi 为卡片窗口 DPI。
// 返回：至少一列的网格；极窄窗口仍保持非零内容宽度。
[[nodiscard]] inline FenceGridLayout MakeFenceGridLayout(int client_width, UINT dpi) {
    constexpr int kHorizontalMargin = 14;
    constexpr int kMinimumCellWidth = 68;
    const int margin = platform::Scale(kHorizontalMargin, dpi);
    const int minimum_cell = std::max(1, platform::Scale(kMinimumCellWidth, dpi));
    const int content_width = std::max(1, client_width - margin * 2);
    const int columns = std::max(1, content_width / minimum_cell);
    // 核心逻辑：每列边界按可用宽度的整数比例求得，余数分散到各列；
    // 首列和末列始终与左右边框保持同样距离，末行也沿用完整网格左对齐。
    return {margin, content_width, columns};
}

}  // desktop_orb::ui 命名空间结束
