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
    // 作用：确定 exe 同级 settings.ini 的存放路径。
    // 参数：无。返回：无；系统无法读取 exe 路径时退回当前工作目录。
    SettingsStore();
    // 作用：取得当前配置文件路径。参数：无。返回：存储对象持有的路径引用。
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    // 作用：读取快捷键、悬浮球位置和截图 AI 参数。参数：无。
    // 返回：解析后的设置；文件或字段缺失、位置无效时使用默认值。
    [[nodiscard]] Settings Load() const;
    // 作用：把全部应用设置写入 exe 同级的统一 INI 文件。
    // 参数：settings 为待保存的快捷键、窗口位置和截图 AI 参数。返回：各项写入成功为 true，否则为 false。
    bool Save(const Settings& settings) const;
    // 作用：把用户快捷键文本解析为 Win32 修饰键和键码。
    // 参数：text 为如 Ctrl+Alt+Space 的组合。返回：有效时为 Hotkey，否则为空。
    [[nodiscard]] static std::optional<Hotkey> ParseHotkey(const std::wstring& text);

private:
    std::filesystem::path path_;
};

}  // desktop_orb::platform 命名空间结束
