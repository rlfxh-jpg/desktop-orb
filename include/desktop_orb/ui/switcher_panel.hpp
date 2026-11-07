#pragma once

#include "desktop_orb/platform/monitor.hpp"
#include "desktop_orb/platform/thumbnail_manager.hpp"
#include "desktop_orb/platform/window_catalog.hpp"
#include "desktop_orb/ui/window_base.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace desktop_orb::ui {

class SwitcherPanel final : public WindowBase {
public:
    enum class Edge { left, right };

    explicit SwitcherPanel(platform::WindowCatalog& catalog) : catalog_(catalog) {}

    std::function<void()> on_closed;
    bool Create();
    void SetExcluded(std::vector<HWND> handles);
    void ShowFor(const platform::Monitor& monitor, Edge edge);
    void Hide();
    [[nodiscard]] bool visible() const { return visible_; }

protected:
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    void Position();
    void Refresh();
    void ScrollTo(int position);
    void SelectRelative(int direction);
    void Activate(size_t index);
    void SyncThumbnails();
    void Paint();
    [[nodiscard]] int Header() const;
    [[nodiscard]] int CardHeight() const;
    [[nodiscard]] int ViewportHeight() const;

    platform::WindowCatalog& catalog_;
    std::unique_ptr<platform::ThumbnailManager> thumbnails_;
    std::vector<platform::WindowInfo> windows_;
    std::vector<HWND> excluded_;
    platform::Monitor monitor_{};
    Edge edge_ = Edge::left;
    HWND active_before_open_ = nullptr;
    int scroll_ = 0;
    int wheel_remainder_ = 0;
    size_t selected_ = 0;
    bool visible_ = false;
};

}  // namespace desktop_orb::ui
