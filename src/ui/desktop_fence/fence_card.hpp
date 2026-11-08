#pragma once

// 文件职责：定义围栏管理器使用的单卡片窗口接口。
// 具体 Win32 消息处理、项目缓存和绘制状态由 fence_card.cpp 私有实现。
#include "desktop_orb/platform/desktop_fence/desktop_item_catalog.hpp"
#include <memory>

namespace desktop_orb::ui {

class FenceCard {
public:
    // 作用：销毁卡片接口对象。参数：无。返回：无。
    virtual ~FenceCard() = default;

    // 作用：创建卡片窗口并绑定桌面项目分类。
    // 参数：category 为该卡片显示的桌面分类。返回：创建成功为 true，否则为 false。
    virtual bool Create(platform::DesktopFenceCategory category) = 0;

    // 作用：定位并显示卡片，同时刷新显示器壁纸与桌面项目。
    // 参数：bounds 为卡片屏幕矩形；monitor_bounds 为所属显示器矩形。
    // 返回：窗口成功定位并显示为 true，否则为 false。
    virtual bool ShowAt(RECT bounds, RECT monitor_bounds) = 0;

    // 作用：隐藏卡片并停止其后台刷新。参数：无。返回：无。
    virtual void Hide() = 0;

    // 作用：读取卡片窗口屏幕矩形。参数：无。
    // 返回：窗口矩形；窗口未创建或查询失败时返回零矩形。
    [[nodiscard]] virtual RECT Bounds() const = 0;

    // 作用：查询卡片是否显示。参数：无。返回：显示时为 true，否则为 false。
    [[nodiscard]] virtual bool visible() const = 0;

    // 作用：取得卡片 HWND，供窗口切换器排除自身窗口。
    // 参数：无。返回：创建成功时的 HWND；创建前或销毁后为空。
    [[nodiscard]] virtual HWND handle() const = 0;
};

// 作用：创建围栏卡片的 Win32 私有实现。
// 参数：无。返回：新卡片对象；内存分配失败时抛出 std::bad_alloc。
[[nodiscard]] std::unique_ptr<FenceCard> MakeFenceCard();

}  // desktop_orb::ui 命名空间结束
