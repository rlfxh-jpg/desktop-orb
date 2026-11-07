#pragma once

#include <memory>
#include <windows.h>

namespace desktop_orb::ui {

// Owns the embedded orb artwork and renders its per-pixel alpha into a layered window.
class OrbArtwork final {
public:
    OrbArtwork();
    ~OrbArtwork();
    OrbArtwork(const OrbArtwork&) = delete;
    OrbArtwork& operator=(const OrbArtwork&) = delete;

    [[nodiscard]] bool ready() const;
    bool Render(HWND window, BYTE opacity) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace desktop_orb::ui
