// 文件职责：将悬浮球图标提交给 Win32 分层窗口，通用绘制过程由 IconImage 负责。
// OrbArtwork 只持有图标，不拥有调用方传入的 HWND。
#include "desktop_orb/ui/orb_artwork.hpp"

namespace desktop_orb::ui {

bool OrbArtwork::Render(HWND window, BYTE opacity) const {
    return image_.RenderLayered(window, opacity);
}

}  // desktop_orb::ui 命名空间结束
