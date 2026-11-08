#pragma once

// 文件职责：定义悬浮球设置数据与 INI 文件读写接口。
// 快捷键文本解析在此模块完成，快捷键注册由应用层负责。
#include <windows.h>
#include <filesystem>
#include <optional>
#include <string>

namespace desktop_orb::platform {

struct Hotkey {
    UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    UINT key = VK_SPACE;
};

struct Settings {
    std::optional<POINT> orb_position;
    std::wstring hotkey = L"Ctrl+Alt+Space";
    std::wstring screenshot_ai_api_key;
    std::wstring screenshot_ai_base_url = L"https://api.openai.com/v1";
    std::wstring screenshot_ai_model = L"gpt-4o-mini";
};

class SettingsStore {
public:
    // 作用：确定 settings.ini 的存放路径。参数：无。返回：无。
    SettingsStore();
    // 作用：取得当前配置文件路径。参数：无。返回：存储对象持有的路径引用。
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    // 作用：读取快捷键和悬浮球位置。参数：无。
    // 返回：解析后的设置；文件缺失或位置无效时使用默认值。
    [[nodiscard]] Settings Load() const;
    // 作用：写入当前设置到 INI 文件。参数：settings 为待保存的设置。
    // 返回：目录创建及各项写入均成功为 true，否则为 false。
    bool Save(const Settings& settings) const;
    // 作用：把用户快捷键文本解析为 Win32 修饰键和键码。
    // 参数：text 为如 Ctrl+Alt+Space 的组合。返回：有效时为 Hotkey，否则为空。
    [[nodiscard]] static std::optional<Hotkey> ParseHotkey(const std::wstring& text);

private:
    std::filesystem::path path_;
};

}  // desktop_orb::platform 命名空间结束
