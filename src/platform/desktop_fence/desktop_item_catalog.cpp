// 文件职责：枚举用户与公共桌面项目，按文件夹、程序、文档和其他项目分类。
// 项目路径由文件系统读取；图标索引取自 Windows Shell 的共享系统图像列表。
#include "desktop_orb/platform/desktop_fence/desktop_item_catalog.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <shlobj.h>
#include <shellapi.h>
#include <unordered_set>

namespace desktop_orb::platform {
namespace {

// 作用：判断文件扩展名是否属于可直接启动的程序或快捷方式。
// 参数：extension 为不区分大小写的扩展名。返回：属于程序类别时为 true。
bool IsProgramExtension(std::wstring extension) {
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    return extension == L".lnk" || extension == L".url" || extension == L".exe" ||
        extension == L".bat" || extension == L".cmd" || extension == L".appref-ms";
}

// 作用：判断扩展名是否属于常见文档、图片、压缩包或媒体文件。
// 参数：extension 为不区分大小写的扩展名。返回：属于文件与文档类别时为 true。
bool IsDocumentExtension(std::wstring extension) {
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    constexpr const wchar_t* extensions[] = {
        L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".txt",
        L".md", L".rtf", L".csv", L".odt", L".ods", L".odp", L".png", L".jpg",
        L".jpeg", L".gif", L".bmp", L".webp", L".svg", L".zip", L".7z", L".rar",
        L".mp3", L".wav", L".mp4", L".mov", L".mkv"
    };
    return std::any_of(std::begin(extensions), std::end(extensions),
        [&extension](const wchar_t* item) { return extension == item; });
}

// 作用：把路径字符串规范成不区分大小写的去重键。
// 参数：path 为待规范化的文件路径。返回：用于当前进程内比较的宽字符串键。
std::wstring DeduplicationKey(const std::filesystem::path& path) {
    std::wstring key = path.lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), towlower);
    return key;
}

// 作用：取得用户桌面和公共桌面目录，并释放 Windows Shell 分配的路径内存。
// 参数：无。返回：成功解析的桌面目录；单个 Known Folder 失败时跳过该目录。
std::vector<std::filesystem::path> DesktopDirectories() {
    std::vector<std::filesystem::path> directories;
    for (const KNOWNFOLDERID* id : {&FOLDERID_Desktop, &FOLDERID_PublicDesktop}) {
        PWSTR raw_path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &raw_path)) && raw_path) {
            directories.emplace_back(raw_path);
            CoTaskMemFree(raw_path);
        }
    }
    return directories;
}

// 作用：根据文件系统属性和扩展名选择围栏分类。
// 参数：path 为项目路径；is_directory 为枚举到的目录属性。返回：对应的桌面围栏类别。
DesktopFenceCategory Classify(const std::filesystem::path& path, bool is_directory) {
    if (is_directory) return DesktopFenceCategory::folders;
    const std::wstring extension = path.extension().wstring();
    if (IsProgramExtension(extension)) return DesktopFenceCategory::programs;
    if (IsDocumentExtension(extension)) return DesktopFenceCategory::documents;
    return DesktopFenceCategory::other;
}

}  // 匿名命名空间结束

std::vector<DesktopItem> DesktopItemCatalog::List(DesktopFenceCategory category) const {
    std::vector<DesktopItem> items;
    std::unordered_set<std::wstring> seen_paths;
    for (const auto& directory : DesktopDirectories()) {
        std::error_code error;
        std::filesystem::directory_iterator iterator(directory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            const auto path = iterator->path();
            const bool is_directory = iterator->is_directory(error);
            if (!error && Classify(path, is_directory) == category &&
                seen_paths.insert(DeduplicationKey(path)).second) {
                SHFILEINFOW info{};
                const DWORD_PTR icon_result = SHGetFileInfoW(path.c_str(), 0, &info,
                    sizeof(info), SHGFI_TYPENAME);
                DesktopItem item;
                item.name = path.filename().wstring();
                item.path = path.wstring();
                item.type_name = icon_result ? info.szTypeName :
                    (is_directory ? L"文件夹" : path.extension().wstring());
                items.push_back(std::move(item));
            }
            error.clear();
            iterator.increment(error);
        }
    }

    std::sort(items.begin(), items.end(), [](const DesktopItem& left, const DesktopItem& right) {
        return CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return items;
}

const wchar_t* DesktopItemCatalog::CategoryName(DesktopFenceCategory category) {
    switch (category) {
        case DesktopFenceCategory::programs: return L"程序";
        case DesktopFenceCategory::folders: return L"文件夹";
        case DesktopFenceCategory::documents: return L"文件与文档";
        case DesktopFenceCategory::other: return L"其他";
    }
    return L"其他";
}

bool DesktopItemCatalog::Open(const std::wstring& path) {
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(),
        nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

HICON DesktopItemCatalog::LoadIcon(const std::wstring& path) {
    SHFILEINFOW info{};
    if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON))
        return nullptr;
    return info.hIcon;
}

HICON DesktopItemCatalog::LoadCategoryIcon(DesktopFenceCategory category) {
    const wchar_t* path = category == DesktopFenceCategory::folders ? L"."
        : category == DesktopFenceCategory::programs ? L".lnk"
        : category == DesktopFenceCategory::documents ? L".txt" : L".dat";
    const DWORD attributes = category == DesktopFenceCategory::folders
        ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    SHFILEINFOW info{};
    if (!SHGetFileInfoW(path, attributes, &info, sizeof(info),
            SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES)) return nullptr;
    return info.hIcon;
}

}  // desktop_orb::platform 命名空间结束
