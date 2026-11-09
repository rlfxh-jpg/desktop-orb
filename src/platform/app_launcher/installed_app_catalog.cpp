// 文件职责：通过 Windows AppsFolder 列出传统桌面程序与打包应用，并由 Shell 启动。
// COM 与 PIDL 仅在调用期间持有；返回的图标所有权转移给 UI 调用方。
#include "desktop_orb/platform/app_launcher/installed_app_catalog.hpp"
#include "desktop_orb/platform/app_launcher/app_entry_rules.hpp"

#include <algorithm>
#include <cwctype>
#include <shlobj.h>
#include <shellapi.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <unordered_set>
#include <wrl/client.h>

namespace desktop_orb::platform {
namespace {

class ComApartment final {
public:
    // 作用：为当前调用初始化 Shell 所需的单线程 COM。
    // 参数：无。返回：无；初始化失败时通过 ready() 查询状态。
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    // 作用：仅在本对象成功初始化 COM 时平衡引用计数。参数：无。返回：无。
    ~ComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
    // 作用：查询当前线程能否调用 Shell COM。参数：无。返回：可使用时为 true。
    [[nodiscard]] bool ready() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
private:
    HRESULT result_;
};

// 作用：把 Shell 分配的字符串复制为独立的 C++ 字符串并释放原内存。
// 参数：pidl 为完整 Shell 项目 ID；kind 为所需名称格式。返回：成功读取的名称，否则为空。
std::wstring ShellName(PCIDLIST_ABSOLUTE pidl, SIGDN kind) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetNameFromIDList(pidl, kind, &raw)) || !raw) return {};
    std::wstring name(raw);
    CoTaskMemFree(raw);
    return name;
}

// 作用：从 Shell 条目属性读取快捷方式的实际目标路径。
// 参数：pidl 为 AppsFolder 中的完整项目 ID。
// 返回：目标路径；无属性或读取失败时为空，打包应用允许为空。
std::wstring TargetPath(PCIDLIST_ABSOLUTE pidl) {
    Microsoft::WRL::ComPtr<IShellItem2> item;
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(item.GetAddressOf())))) return {};
    PWSTR raw = nullptr;
    if (FAILED(item->GetString(PKEY_Link_TargetParsingPath, &raw)) || !raw) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    std::wstring target(raw);
    CoTaskMemFree(raw);
    return target;
}

// 作用：把 AppsFolder 根目录或其中应用的解析名称转换为完整 PIDL。
// 参数：parsing_name 为根目录名称或应用 ID。返回：成功时为 PIDL，失败时为空；由调用方 CoTaskMemFree。
PIDLIST_ABSOLUTE ParseApp(const std::wstring& parsing_name) {
    // 核心逻辑：AppsFolder 返回的桌面绝对解析名可能只是应用 ID，例如
    // Microsoft.WindowsNotepad_...!App；直接交给 SHParseDisplayName 会被当作文件路径。
    // 明确加上 AppsFolder 命名空间前缀，启动和取图标才能指向同一个 Shell 项目。
    const std::wstring full_name = parsing_name == L"shell:AppsFolder"
        ? parsing_name : L"shell:AppsFolder\\" + parsing_name;
    PIDLIST_ABSOLUTE pidl = nullptr;
    return SUCCEEDED(SHParseDisplayName(full_name.c_str(), nullptr, &pidl, 0, nullptr))
        ? pidl : nullptr;
}

// 作用：识别 Windows 打包应用常用的“包名_13 位发布者 ID!入口”解析名。
// 参数：parsing_name 为 AppsFolder 返回的解析名。
// 返回：匹配包应用 ID 为 packaged；其余条目保守归为 desktop。
AppKind ClassifyApp(const std::wstring& parsing_name) {
    const size_t separator = parsing_name.rfind(L'!');
    if (separator == std::wstring::npos || separator + 1 == parsing_name.size())
        return AppKind::desktop;
    const size_t publisher = parsing_name.rfind(L'_', separator);
    if (publisher == std::wstring::npos || separator - publisher != 14 || publisher == 0)
        return AppKind::desktop;
    for (size_t index = publisher + 1; index < separator; ++index) {
        const wchar_t character = parsing_name[index];
        if (!std::iswalnum(character)) return AppKind::desktop;
    }
    return AppKind::packaged;
}

}  // 匿名命名空间结束

// 作用：枚举 Windows AppsFolder 中的应用，并按名称稳定排序。
// 参数：error 接收失败说明。返回：可启动应用列表；某个条目无名称时跳过。
std::vector<InstalledApp> InstalledAppCatalog::List(std::wstring& error) const {
    error.clear();
    ComApartment apartment;
    if (!apartment.ready()) { error = L"无法初始化 Windows Shell。"; return {}; }
    PIDLIST_ABSOLUTE folder_pidl = ParseApp(L"shell:AppsFolder");
    if (!folder_pidl) { error = L"无法读取 Windows 应用目录。"; return {}; }
    Microsoft::WRL::ComPtr<IShellFolder> folder;
    const HRESULT bound = SHBindToObject(nullptr, folder_pidl, nullptr,
        IID_PPV_ARGS(folder.GetAddressOf()));
    if (FAILED(bound)) {
        CoTaskMemFree(folder_pidl);
        error = L"无法打开 Windows 应用目录。";
        return {};
    }
    Microsoft::WRL::ComPtr<IEnumIDList> enumerator;
    const HRESULT listed = folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS,
        enumerator.GetAddressOf());
    if (FAILED(listed) || !enumerator) {
        CoTaskMemFree(folder_pidl);
        error = L"无法枚举已安装应用。";
        return {};
    }

    std::vector<AppEntryCandidate> candidates;
    std::unordered_set<std::wstring> seen;
    PITEMID_CHILD child = nullptr;
    while (enumerator->Next(1, &child, nullptr) == S_OK) {
        PIDLIST_ABSOLUTE absolute = ILCombine(folder_pidl, child);
        if (absolute) {
            InstalledApp app{ShellName(absolute, SIGDN_NORMALDISPLAY),
                             ShellName(absolute, SIGDN_DESKTOPABSOLUTEPARSING)};
            app.kind = ClassifyApp(app.parsing_name);
            if (!app.name.empty() && !app.parsing_name.empty() &&
                seen.insert(app.parsing_name).second)
                candidates.push_back({std::move(app), TargetPath(absolute)});
            CoTaskMemFree(absolute);
        }
        CoTaskMemFree(child);
        child = nullptr;
    }
    CoTaskMemFree(folder_pidl);
    return SelectLaunchableApps(std::move(candidates));
}

// 作用：使用 Shell PIDL 启动 AppsFolder 项目，兼容桌面程序和打包应用。
// 参数：app 为应用。返回：ShellExecuteExW 接受请求为 true，否则为 false。
bool InstalledAppCatalog::Launch(const InstalledApp& app) {
    ComApartment apartment;
    if (!apartment.ready()) return false;
    PIDLIST_ABSOLUTE pidl = ParseApp(app.parsing_name);
    if (!pidl) return false;
    SHELLEXECUTEINFOW request{sizeof(request)};
    request.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"open";
    request.lpIDList = pidl;
    request.nShow = SW_SHOWNORMAL;
    const bool launched = ShellExecuteExW(&request) != FALSE;
    CoTaskMemFree(pidl);
    return launched;
}

// 作用：从 Shell 命名空间取得应用的小图标。
// 参数：app 为应用。返回：由调用方持有的图标句柄，解析或读取失败时为空。
HICON InstalledAppCatalog::LoadIcon(const InstalledApp& app) {
    ComApartment apartment;
    if (!apartment.ready()) return nullptr;
    PIDLIST_ABSOLUTE pidl = ParseApp(app.parsing_name);
    if (!pidl) return nullptr;
    SHFILEINFOW info{};
    const DWORD_PTR result = SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &info,
        sizeof(info), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON);
    CoTaskMemFree(pidl);
    return result ? info.hIcon : nullptr;
}

}  // desktop_orb::platform 命名空间结束
