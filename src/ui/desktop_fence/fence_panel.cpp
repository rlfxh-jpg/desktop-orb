// 文件职责：协调四张桌面围栏卡片的创建、显示位置、统一关闭与窗口排除信息。
// 卡片窗口、桌面项目、壁纸和内容绘制分别由 desktop_fence 子模块负责。
#include "desktop_orb/ui/desktop_fence/fence_panel.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include "desktop_orb/platform/desktop_fence/desktop_item_catalog.hpp"
#include "fence_card.hpp"
#include <algorithm>

namespace desktop_orb::ui {
namespace {

constexpr int kCardWidth = 600;
constexpr int kCardHeight = 450;
constexpr int kCardGap = 16;
constexpr platform::DesktopFenceCategory kCategories[] = {
    platform::DesktopFenceCategory::programs,
    platform::DesktopFenceCategory::folders,
    platform::DesktopFenceCategory::documents,
    platform::DesktopFenceCategory::other
};

}  // 匿名命名空间结束



// 作用：销毁四个卡片对象及各自持有的窗口。参数：无。返回：无。
FencePanel::FencePanel() = default;
FencePanel::~FencePanel() = default;

// 作用：初始化公共控件并创建四张分类卡片。
// 参数：无。返回：全部卡片成功创建为 true；任一失败则释放已创建卡片并返回 false。
bool FencePanel::Create() {
    for (size_t index = 0; index < cards_.size(); ++index) {
        cards_[index] = MakeFenceCard();
        if (!cards_[index]->Create(kCategories[index])) {
            cards_ = {};
            return false;
        }
    }
    return true;
}

// 作用：在指定显示器工作区内展示四张围栏卡片。
// 参数：monitor 为悬浮球所在显示器；align_right 为卡片组靠右排列标志。
// 返回：无；若卡片组已显示则保持打开，若空间不足则按工作区缩小卡片。
void FencePanel::ShowFor(const platform::Monitor& monitor, bool align_right) {
    if (visible_) return;
    const int work_width = monitor.work.right - monitor.work.left;
    const int work_height = monitor.work.bottom - monitor.work.top;
    const UINT dpi = monitor.dpi ? monitor.dpi : 96;
    const int gap = std::min(platform::Scale(kCardGap, dpi),
        std::max(1, std::min(work_width, work_height) / 12));
    const int card_width = std::min(platform::Scale(kCardWidth, dpi),
                                    std::max(1, (work_width - gap * 3) / 2));
    const int card_height = std::min(platform::Scale(kCardHeight, dpi),
                                     std::max(1, (work_height - gap * 3) / 2));
    const int group_width = card_width * 2 + gap;
    const int group_height = card_height * 2 + gap;
    const int group_left = align_right ? monitor.work.right - group_width
                                       : monitor.work.left + gap;
    const int group_top = monitor.work.top + std::max(0, (work_height - group_height) / 2);
    bool all_cards_shown = true;
    for (size_t index = 0; index < cards_.size(); ++index) {
        const int column = static_cast<int>(index % 2);
        const int row = static_cast<int>(index / 2);
        const int left = group_left + column * (card_width + gap);
        const int top = group_top + row * (card_height + gap);
        all_cards_shown = cards_[index]->ShowAt(
            {left, top, left + card_width, top + card_height}, monitor.bounds) && all_cards_shown;
    }
    if (!all_cards_shown) {
        for (const auto& card : cards_) {
            if (card) card->Hide();
        }
        return;
    }
    // 核心逻辑：卡片全部显示成功后才隐藏桌面图标；服务记录原始可见状态，
    // 围栏关闭或对象销毁时恢复，避免覆盖用户此前手动隐藏桌面图标的设置。
    desktop_icon_visibility_.Hide();
    visible_ = true;
}

// 作用：隐藏卡片并通知应用层恢复悬浮球。
// 参数：无。返回：无；卡片原本隐藏时不重复通知。
void FencePanel::Hide() {
    if (!visible_) return;
    visible_ = false;
    for (const auto& card : cards_) {
        if (card) card->Hide();
    }
    desktop_icon_visibility_.Restore();
    if (on_closed) on_closed();
}

// 作用：收集已创建卡片的窗口句柄。
// 参数：无。返回：非空的卡片 HWND 列表，未创建的卡片会被跳过。
std::vector<HWND> FencePanel::Handles() const {
    std::vector<HWND> handles;
    handles.reserve(cards_.size());
    for (const auto& card : cards_) {
        if (card && card->handle()) handles.push_back(card->handle());
    }
    return handles;
}

// 作用：判断屏幕坐标是否位于当前可见卡片内。
// 参数：point 为屏幕坐标。返回：命中任意可见卡片为 true，否则为 false。
bool FencePanel::Contains(POINT point) const {
    for (const auto& card : cards_) {
        if (!card || !card->visible()) continue;
        const RECT bounds = card->Bounds();
        if (point.x >= bounds.left && point.x < bounds.right &&
            point.y >= bounds.top && point.y < bounds.bottom) return true;
    }
    return false;
}

}  // desktop_orb::ui 命名空间结束
