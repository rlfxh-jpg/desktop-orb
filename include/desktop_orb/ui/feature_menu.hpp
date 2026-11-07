#pragma once

#include "desktop_orb/platform/monitor.hpp"
#include "desktop_orb/ui/window_base.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace desktop_orb::ui {

class FeatureMenu {
public:
    FeatureMenu();
    ~FeatureMenu();
    bool Add(std::wstring title, std::function<void()> action);
    void ShowAround(RECT orb, const platform::Monitor& monitor, UINT dpi);
    void Hide();
    [[nodiscard]] bool visible() const { return visible_; }
    [[nodiscard]] std::vector<HWND> Handles() const;

private:
    class Button;
    std::vector<std::unique_ptr<Button>> buttons_;
    bool visible_ = false;
};

}  // namespace desktop_orb::ui
