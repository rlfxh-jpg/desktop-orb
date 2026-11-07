// 文件职责：计算环绕菜单的入口位置，保证放大后的按钮互不遮挡并留在显示器工作区。
// 优先使用朝屏幕内侧的圆弧；可用高度不足时排成多列，且不持有窗口资源。
#include "desktop_orb/ui/common/feature_layout.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace desktop_orb::ui {
namespace {

constexpr int kRadialLogicalGap = 6;
constexpr int kNeighborLogicalGap = 8;
constexpr int kRadiusGrowthPerFeature = 6;
constexpr double kMaxArcAngle = 70.0 * std::numbers::pi / 180.0;

}  // 匿名命名空间结束

FeatureLayout ArrangeFeatures(RECT orb, RECT work, UINT dpi, size_t count) {
    FeatureLayout layout{platform::Scale(kDefaultFeatureDiameter, dpi),
                         platform::Scale(kHoveredFeatureDiameter, dpi), {}};
    const int work_width = work.right - work.left;
    const int work_height = work.bottom - work.top;
    if (!count || work_width <= 0 || work_height <= 0) return layout;

    layout.hovered_diameter = std::min({layout.hovered_diameter, work_width, work_height});
    layout.default_diameter = std::min(layout.default_diameter, layout.hovered_diameter);
    const int hovered = layout.hovered_diameter;
    const int spacing = hovered + std::max(1, platform::Scale(kNeighborLogicalGap, dpi));
    const int min_x = work.left + hovered / 2;
    const int max_x = work.right - (hovered + 1) / 2;
    const int min_y = work.top + hovered / 2;
    const int max_y = work.bottom - (hovered + 1) / 2;
    const int orb_x = (orb.left + orb.right) / 2;
    const int orb_y = (orb.top + orb.bottom) / 2;
    const int direction = orb_x < (work.left + work.right) / 2 ? 1 : -1;
    const double orb_radius = std::max(orb.right - orb.left, orb.bottom - orb.top) / 2.0;
    const double minimum_radius = orb_radius + hovered / 2.0 +
                                  platform::Scale(kRadialLogicalGap, dpi);
    const double half_span = (static_cast<double>(count) - 1.0) * spacing / 2.0;

    if (2.0 * half_span <= max_y - min_y) {
        // 核心逻辑：沿圆弧布置，但用放大后的直径确定纵向间距；边缘处整体偏移，避免逐个裁切后重叠。
        const double shift = std::clamp(0.0, min_y - orb_y + half_span,
                                       max_y - orb_y - half_span);
        const double furthest = std::max(std::abs(shift - half_span), std::abs(shift + half_span));
        const double radius = std::max(minimum_radius +
                                       (static_cast<double>(count) - 1.0) *
                                           platform::Scale(kRadiusGrowthPerFeature, dpi),
                                       furthest / std::sin(kMaxArcAngle));
        for (size_t index = 0; index < count; ++index) {
            const double offset = (static_cast<double>(index) - (static_cast<double>(count) - 1.0) / 2.0) *
                                  spacing + shift;
            const int x = static_cast<int>(std::lround(orb_x + direction *
                std::sqrt(std::max(0.0, radius * radius - offset * offset))));
            const int y = static_cast<int>(std::lround(orb_y + offset));
            if (x < min_x || x > max_x || y < min_y || y > max_y) break;
            layout.centers.push_back({x, y});
        }
        if (layout.centers.size() == count) return layout;
        layout.centers.clear();
    }

    // 核心逻辑：圆弧放不进工作区时改为网格；行列间距仍按放大后的完整按钮尺寸计算。
    const int rows = 1 + (max_y - min_y) / spacing;
    const int first_x = std::clamp(static_cast<int>(std::lround(orb_x + direction * minimum_radius)),
                                   min_x, max_x);
    for (size_t index = 0; index < count; ++index) {
        const int column = static_cast<int>(index / rows);
        const int row = static_cast<int>(index % rows);
        const int x = first_x + direction * column * spacing;
        if (x < min_x || x > max_x) {
            layout.centers.clear();
            return layout;
        }
        const int items_in_column = static_cast<int>(std::min(count - static_cast<size_t>(column) * rows,
                                                               static_cast<size_t>(rows)));
        const int top = std::clamp(orb_y - (items_in_column - 1) * spacing / 2,
                                   min_y, max_y - (items_in_column - 1) * spacing);
        layout.centers.push_back({x, top + row * spacing});
    }
    return layout;
}

}  // desktop_orb::ui 命名空间结束
