// 文件职责：定位和读写 INI 设置，并解析全局快捷键文本。
// 本模块不注册快捷键；注册失败的交互反馈由 App 负责。
#include "desktop_orb/platform/settings.hpp"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace desktop_orb::platform {
namespace {

// 作用：读取 INI 中的单个字符串配置。
// 参数：path 为配置文件；section 为分组；key 为键；fallback 为缺失时默认值。
// 返回：读取到的宽字符串，最多保留缓冲区可容纳的内容。
std::wstring ReadValue(const std::filesystem::path& path, const wchar_t* section,
                       const wchar_t* key, const wchar_t* fallback) {
    wchar_t buffer[256]{};
    GetPrivateProfileStringW(section, key, fallback, buffer, 256, path.c_str());
    return buffer;
}

// 作用：去除首尾空白并转为大写，以便快捷键名称不区分大小写。
// 参数：value 为待规范化文本。返回：规范化文本；全空白时为空。
std::wstring UpperTrim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t");
    value = value.substr(first, last - first + 1);
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towupper(c); });
    return value;
}

}  // 匿名命名空间结束

SettingsStore::SettingsStore() {
    wchar_t appdata[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, 32768);
    path_ = length > 0 && length < 32768
        ? std::filesystem::path(appdata) / L"DesktopOrb" / L"settings.ini"
        : std::filesystem::current_path() / L"settings.ini";
}

Settings SettingsStore::Load() const {
    Settings settings;
    settings.hotkey = ReadValue(path_, L"Controls", L"Hotkey", L"Ctrl+Alt+Space");
    const auto x = ReadValue(path_, L"Orb", L"X", L"");
    const auto y = ReadValue(path_, L"Orb", L"Y", L"");
    if (!x.empty() && !y.empty()) {
        try { settings.orb_position = POINT{std::stoi(x), std::stoi(y)}; }
        catch (const std::exception&) { settings.orb_position.reset(); }
    }
    return settings;
}

bool SettingsStore::Save(const Settings& settings) const {
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) return false;
    if (!WritePrivateProfileStringW(L"Controls", L"Hotkey", settings.hotkey.c_str(), path_.c_str())) return false;
    if (settings.orb_position) {
        const auto x = std::to_wstring(settings.orb_position->x);
        const auto y = std::to_wstring(settings.orb_position->y);
        if (!WritePrivateProfileStringW(L"Orb", L"X", x.c_str(), path_.c_str())) return false;
        if (!WritePrivateProfileStringW(L"Orb", L"Y", y.c_str(), path_.c_str())) return false;
    }
    return true;
}

std::optional<Hotkey> SettingsStore::ParseHotkey(const std::wstring& text) {
    Hotkey result{MOD_NOREPEAT, 0};
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find(L'+', start);
        const std::wstring part = UpperTrim(text.substr(start, end == std::wstring::npos ? end : end - start));
        if (part == L"CTRL" || part == L"CONTROL") result.modifiers |= MOD_CONTROL;
        else if (part == L"ALT") result.modifiers |= MOD_ALT;
        else if (part == L"SHIFT") result.modifiers |= MOD_SHIFT;
        else if (part == L"WIN" || part == L"WINDOWS") result.modifiers |= MOD_WIN;
        else if (result.key == 0 && part == L"SPACE") result.key = VK_SPACE;
        else if (result.key == 0 && part.size() == 1 &&
                 ((part[0] >= L'A' && part[0] <= L'Z') || (part[0] >= L'0' && part[0] <= L'9'))) {
            result.key = static_cast<UINT>(part[0]);
        } else if (result.key == 0 && part.size() >= 2 && part[0] == L'F') {
            try {
                size_t consumed = 0;
                const int number = std::stoi(part.substr(1), &consumed);
                if (consumed != part.size() - 1) return std::nullopt;
                if (number < 1 || number > 12) return std::nullopt;
                result.key = VK_F1 + number - 1;
            } catch (const std::exception&) { return std::nullopt; }
        } else return std::nullopt;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    if (result.key == 0 || result.modifiers == MOD_NOREPEAT) return std::nullopt;
    return result;
}

}  // desktop_orb::platform 命名空间结束
