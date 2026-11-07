#pragma once

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
};

class SettingsStore {
public:
    SettingsStore();
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] Settings Load() const;
    bool Save(const Settings& settings) const;
    [[nodiscard]] static std::optional<Hotkey> ParseHotkey(const std::wstring& text);

private:
    std::filesystem::path path_;
};

}  // namespace desktop_orb::platform
