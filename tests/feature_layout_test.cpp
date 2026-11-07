// 文件职责：验证环绕菜单在多入口、屏幕边缘和高 DPI 情况下的几何约束。
// 测试只调用布局函数，不创建实际窗口或修改桌面状态。
#include "desktop_orb/ui/common/feature_layout.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

// 作用：在布局约束不满足时报告原因并终止测试。
// 参数：condition 为待检查条件；message 为失败时输出的说明。返回：无；失败时进程退出码为 1。
void Require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// 作用：检查所有入口的放大边界、与悬浮球的距离及彼此间距。
// 参数：orb 为悬浮球矩形；work 为显示器工作区；dpi 为缩放值；count 为入口数量。
// 返回：布局结果；任何约束失败时测试进程退出。
desktop_orb::ui::FeatureLayout CheckLayout(RECT orb, RECT work, UINT dpi, size_t count) {
    const auto layout = desktop_orb::ui::ArrangeFeatures(orb, work, dpi, count);
    Require(layout.centers.size() == count, "入口数量不完整");
    if (!count) return layout;

    const int hovered = layout.hovered_diameter;
    const int gap = desktop_orb::platform::Scale(8, dpi);
    const double orb_x = (orb.left + orb.right) / 2.0;
    const double orb_y = (orb.top + orb.bottom) / 2.0;
    const double orb_radius = std::max(orb.right - orb.left, orb.bottom - orb.top) / 2.0;
    for (size_t index = 0; index < count; ++index) {
        const POINT center = layout.centers[index];
        Require(center.x - hovered / 2 >= work.left &&
                center.x + (hovered + 1) / 2 <= work.right &&
                center.y - hovered / 2 >= work.top &&
                center.y + (hovered + 1) / 2 <= work.bottom,
                "放大后的入口越出工作区");
        Require(std::hypot(center.x - orb_x, center.y - orb_y) + 1 >=
                orb_radius + hovered / 2.0 + desktop_orb::platform::Scale(6, dpi),
                "入口与悬浮球过近");
        for (size_t other = 0; other < index; ++other) {
            const POINT previous = layout.centers[other];
            Require(std::abs(center.x - previous.x) >= hovered + gap ||
                    std::abs(center.y - previous.y) >= hovered + gap,
                    "放大后的入口相互遮挡");
        }
    }
    return layout;
}

}  // 匿名命名空间结束

// 作用：运行单入口、多入口、靠近工作区边缘和高 DPI 的布局检查。
// 参数：无。返回：所有场景通过为 0，任何约束失败为 1。
int main() {
    const RECT work{0, 0, 1920, 1080};
    const RECT left_orb{0, 300, 58, 358};
    const auto one = CheckLayout(left_orb, work, 96, 1);
    Require(one.centers.front().x - 29 < 75, "单入口离悬浮球过远");
    const auto five = CheckLayout(left_orb, work, 96, 5);
    Require(std::hypot(five.centers[2].x - 29, five.centers[2].y - 329) >
            std::hypot(one.centers[0].x - 29, one.centers[0].y - 329),
            "入口增加后布局半径没有增大");
    for (size_t count : {size_t{2}, size_t{3}, size_t{20}})
        CheckLayout(left_orb, work, 96, count);
    CheckLayout({1862, 0, 1920, 58}, work, 96, 5);
    CheckLayout({0, 1022, 58, 1080}, work, 96, 5);
    CheckLayout({0, 300, 87, 387}, work, 144, 5);
    CheckLayout({0, 0, 58, 58}, {0, 0, 240, 160}, 96, 4);
    return 0;
}
