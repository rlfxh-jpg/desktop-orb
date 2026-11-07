#pragma once

#include "desktop_orb/platform/settings.hpp"
#include "desktop_orb/platform/window_catalog.hpp"
#include "desktop_orb/ui/feature_menu.hpp"
#include "desktop_orb/ui/orb_window.hpp"
#include "desktop_orb/ui/switcher_panel.hpp"

namespace desktop_orb {

class App {
public:
    App();
    ~App();
    bool Initialize();
    int Run();

private:
    void Tick();
    void ToggleMenu();
    void OpenSwitcher();
    void CloseOverlays();
    void ShowContextMenu();
    void ReloadSettings();
    void SavePosition(POINT position);
    bool RegisterShortcut(const platform::Hotkey& hotkey);

    platform::SettingsStore settings_store_;
    platform::Settings settings_;
    platform::WindowCatalog catalog_;
    ui::OrbWindow orb_;
    ui::FeatureMenu menu_;
    ui::SwitcherPanel panel_;
    bool hotkey_registered_ = false;
    bool previous_left_down_ = false;
    bool previous_escape_down_ = false;
};

}  // namespace desktop_orb
