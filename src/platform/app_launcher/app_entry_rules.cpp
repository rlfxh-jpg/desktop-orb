// 文件职责：根据 Shell 目标类型筛除文档入口，并为同一安装目录中的架构变体选默认入口。
// 本模块不调用 Shell，也不持有系统资源；可用纯数据进行规则测试。
#include "desktop_orb/platform/app_launcher/app_entry_rules.hpp"

#include <algorithm>
#include <cwctype>
#include <unordered_map>

namespace desktop_orb::platform {
namespace {

// 作用：把名称或路径统一为不区分大小写的比较形式。
// 参数：value 为原始字符串。返回：转换成小写的副本。
std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t character) { return std::towlower(character); });
    return value;
}

// 作用：识别名称末尾明确表示架构或指令集的变体后缀。
// 参数：name 为应用显示名称；base 接收去掉后缀的名称。
// 返回：识别为变体时为 true；其他括号内容保留为独立应用名称。
bool ArchitectureBase(const std::wstring& name, std::wstring& base) {
    if (name.empty() || name.back() != L')') return false;
    const size_t open = name.rfind(L'(');
    if (open == std::wstring::npos || open == 0) return false;
    const std::wstring suffix = Lower(name.substr(open + 1, name.size() - open - 2));
    const bool architecture = suffix == L"x86" || suffix == L"x64" ||
        suffix == L"arm64" || suffix == L"x86_64" ||
        suffix.starts_with(L"32-bit") || suffix.starts_with(L"64-bit") ||
        suffix.starts_with(L"32 bit") || suffix.starts_with(L"64 bit");
    if (!architecture) return false;
    base = name.substr(0, open);
    while (!base.empty() && std::iswspace(base.back())) base.pop_back();
    return !base.empty();
}

// 作用：为同一程序的入口设定保留优先级。
// 参数：name 为显示名称。返回：通用名称为 0，64 位为 1，32 位为 2，特殊指令集为 3。
int VariantRank(const std::wstring& name) {
    std::wstring base;
    if (!ArchitectureBase(name, base)) return 0;
    const std::wstring lower = Lower(name);
    if (lower.find(L"sse") != std::wstring::npos ||
        lower.find(L"avx") != std::wstring::npos) return 3;
    return lower.find(L"32-bit") != std::wstring::npos ||
        lower.find(L"32 bit") != std::wstring::npos ||
        lower.find(L"(x86)") != std::wstring::npos ? 2 : 1;
}

// 作用：生成同一程序的比较键，只有名称及安装位置都一致才合并。
// 参数：candidate 为应用及目标路径。返回：不区分大小写的稳定键。
std::wstring ProgramKey(const AppEntryCandidate& candidate) {
    std::wstring base;
    if (!ArchitectureBase(candidate.app.name, base)) base = candidate.app.name;
    std::wstring location;
    if (candidate.app.kind == AppKind::packaged) {
        const size_t separator = candidate.app.parsing_name.find(L'!');
        location = candidate.app.parsing_name.substr(0, separator);
    } else {
        const size_t slash = candidate.target_path.find_last_of(L"\\/");
        location = candidate.target_path.substr(0, slash);
    }
    return Lower(location + L"\n" + base);
}

// 作用：判断 Shell 条目是否指向真正的可执行程序。
// 参数：candidate 为应用及 Shell 提供的目标路径。
// 返回：打包应用或目标扩展名为 .exe 的桌面程序为 true。
bool IsProgram(const AppEntryCandidate& candidate) {
    if (candidate.app.name.empty() || candidate.app.parsing_name.empty()) return false;
    if (candidate.app.kind == AppKind::packaged) return true;
    const std::wstring& target = candidate.target_path;
    return target.size() >= 4 &&
        Lower(target.substr(target.size() - 4)) == L".exe";
}

}  // 匿名命名空间结束

// 作用：过滤非程序 Shell 条目，并为同目录的架构变体选出一个默认入口。
// 参数：candidates 为枚举结果。返回：去重后按显示名称排序的程序列表。
std::vector<InstalledApp> SelectLaunchableApps(std::vector<AppEntryCandidate> candidates) {
    std::unordered_map<std::wstring, size_t> chosen;
    std::vector<AppEntryCandidate> selected;
    for (auto& candidate : candidates) {
        if (!IsProgram(candidate)) continue;
        std::wstring key = ProgramKey(candidate);
        const auto [found, inserted] = chosen.emplace(std::move(key), selected.size());
        if (inserted) {
            selected.push_back(std::move(candidate));
            continue;
        }
        auto& previous = selected[found->second];
        const int old_rank = VariantRank(previous.app.name);
        const int new_rank = VariantRank(candidate.app.name);
        // 核心逻辑：通用入口优先；名称等级相同则保留首次枚举的可启动条目。
        if (new_rank < old_rank) previous = std::move(candidate);
    }
    std::vector<InstalledApp> apps;
    apps.reserve(selected.size());
    for (auto& candidate : selected) apps.push_back(std::move(candidate.app));
    std::sort(apps.begin(), apps.end(), [](const InstalledApp& left, const InstalledApp& right) {
        const int order = CompareStringOrdinal(left.name.c_str(), -1,
            right.name.c_str(), -1, TRUE);
        return order == CSTR_LESS_THAN ||
            (order == CSTR_EQUAL && left.parsing_name < right.parsing_name);
    });
    return apps;
}

}  // desktop_orb::platform 命名空间结束
