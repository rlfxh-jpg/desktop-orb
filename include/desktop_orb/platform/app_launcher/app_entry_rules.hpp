#pragma once

// 文件职责：定义 AppsFolder 条目的可执行性筛选与同程序变体合并规则。
// 规则只处理 Shell 已提供的名称、目标路径和启动标识；不读取窗口或启动程序。
#include "desktop_orb/platform/app_launcher/installed_app_catalog.hpp"
#include <string>
#include <vector>

namespace desktop_orb::platform {

struct AppEntryCandidate {
    InstalledApp app;
    std::wstring target_path;
};

// 作用：从 AppsFolder 候选条目中保留程序并合并同目录下的架构变体。
// 参数：candidates 为应用数据与 Shell 目标路径；目标为空仅允许打包应用。
// 返回：按显示名称排序的程序列表；文档、链接与重复架构入口被排除。
[[nodiscard]] std::vector<InstalledApp> SelectLaunchableApps(
    std::vector<AppEntryCandidate> candidates);

}  // desktop_orb::platform 命名空间结束
