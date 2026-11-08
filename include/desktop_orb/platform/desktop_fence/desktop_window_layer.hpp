#pragma once

// 文件职责：把桌面围栏窗口放入桌面层级，使普通应用窗口保持在围栏上方。
// 函数只调整已有 HWND 的 Z 序，不拥有或销毁调用方窗口。
#include <windows.h>

namespace desktop_orb::platform {

// 作用：将窗口放到 Explorer 桌面宿主之上、桌面应用之下。
// 参数：window 为待定位的围栏顶层窗口。返回：成功定位为 true；未找到桌面宿主或系统拒绝时为 false。
[[nodiscard]] bool PlaceBehindApplications(HWND window);

}  // desktop_orb::platform 命名空间结束
