// 文件职责：验证 AppsFolder 中非程序目标被排除，且架构变体只保留默认入口。
// 测试使用固定的 Shell 元数据样本，不启动程序或修改系统应用目录。
#include "desktop_orb/platform/app_launcher/app_entry_rules.hpp"
#include "desktop_orb/platform/app_launcher/installed_app_catalog.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

// 作用：检查候选条目处理结果。参数：condition 为条件；message 为失败说明。
// 返回：无；失败时测试进程以状态码 1 退出。
void Require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// 作用：构造桌面程序或文档入口。参数：name 为名称；file 为同目录目标文件名。
// 返回：用于规则测试的 AppsFolder 候选条目。
desktop_orb::platform::AppEntryCandidate Desktop(const wchar_t* name, const wchar_t* file) {
    const std::wstring path = std::wstring(L"C:\\Tools\\Cheat Engine\\") + file;
    return {{name, path, desktop_orb::platform::AppKind::desktop}, path};
}

}  // 匿名命名空间结束

// 作用：验证程序筛选、变体合并；传入 --live 时另外检查当前 Windows AppsFolder。
// 参数：argc 为参数个数；argv 为参数数组。返回：通过为 0；失败时由 Require 退出进程。
int main(int argc, char** argv) {
    using desktop_orb::platform::AppEntryCandidate;
    using desktop_orb::platform::AppKind;
    using desktop_orb::platform::SelectLaunchableApps;
    std::vector<AppEntryCandidate> candidates;
    candidates.push_back(Desktop(L"Cheat Engine (64-bit)", L"cheatengine-x86_64.exe"));
    candidates.push_back(Desktop(L"Cheat Engine (32-bit)", L"cheatengine-i386.exe"));
    candidates.push_back(Desktop(L"Cheat Engine (64-bit SSE4-AVX2)",
        L"cheatengine-x86_64-SSE4-AVX2.exe"));
    candidates.push_back(Desktop(L"Cheat Engine", L"Cheat Engine.exe"));
    candidates.push_back(Desktop(L"Cheat Engine help", L"CheatEngine.chm"));
    candidates.push_back({{L"Notepad", L"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App",
        AppKind::packaged}, L""});
    const auto selected = SelectLaunchableApps(std::move(candidates));
    Require(selected.size() == 2, "程序或帮助文件筛选数量错误");
    Require(selected[0].name == L"Cheat Engine", "没有优先保留通用入口");
    Require(selected[1].name == L"Notepad", "打包应用被错误排除");
    const auto no_default = SelectLaunchableApps({
        Desktop(L"Example (32-bit)", L"example-x86.exe"),
        Desktop(L"Example (64-bit)", L"example-x64.exe")});
    Require(no_default.size() == 1 && no_default[0].name == L"Example (64-bit)",
        "缺少通用入口时没有保留常规 64 位程序");
    if (argc > 1 && std::string(argv[1]) == "--live") {
        desktop_orb::platform::InstalledAppCatalog catalog;
        std::wstring error;
        const auto apps = catalog.List(error);
        Require(error.empty(), "无法枚举当前系统应用");
        const auto count_name = [&apps](const wchar_t* name) {
            return std::count_if(apps.begin(), apps.end(), [name](const auto& app) {
                return app.name == name;
            });
        };
        Require(count_name(L"7-Zip Help") == 0, "帮助文件仍出现在程序列表");
        const size_t variants = std::count_if(apps.begin(), apps.end(), [](const auto& app) {
            return app.name.starts_with(L"Cheat Engine (");
        });
        Require(variants == 0 || count_name(L"Cheat Engine") == 0,
            "同一程序的架构变体没有合并");
        std::cout << "live apps: " << apps.size() << "\n";
    }
    return 0;
}
