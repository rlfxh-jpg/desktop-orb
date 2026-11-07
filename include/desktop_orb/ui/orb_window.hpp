#pragma once

#include "desktop_orb/ui/orb_artwork.hpp"
#include "desktop_orb/ui/window_base.hpp"
#include <functional>

namespace desktop_orb::ui {

class OrbWindow final : public WindowBase {
public:
    std::function<void()> on_click;
    std::function<void()> on_drag_start;
    std::function<void(POINT)> on_moved;
    std::function<void()> on_tick;
    std::function<void()> on_hotkey;
    std::function<void()> on_context;

    bool Create(POINT initial);
    void SetAlpha(BYTE alpha);
    void SnapToEdge();
    [[nodiscard]] RECT Bounds() const;

protected:
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    OrbArtwork artwork_;
    POINT drag_cursor_{};
    RECT drag_origin_{};
    bool dragging_ = false;
    bool moved_ = false;
    BYTE alpha_ = 0;
};

}  // namespace desktop_orb::ui
