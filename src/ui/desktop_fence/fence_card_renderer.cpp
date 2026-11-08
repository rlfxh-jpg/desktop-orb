// 文件职责：将显示器壁纸映射到桌面围栏卡片并合成浅色玻璃表面。
// 本模块不获取壁纸、不拥有窗口，也不绘制标题与图标网格。
#include "fence_card_renderer.hpp"

#include "desktop_orb/platform/common/monitor.hpp"
#include <algorithm>

namespace desktop_orb::ui {
namespace {

constexpr BYTE kGlassAlpha = 190;
constexpr COLORREF kFallbackSurface = RGB(244, 247, 248);
constexpr COLORREF kBorderColor = RGB(255, 255, 255);

struct GdiPlusRuntime {
    ULONG_PTR token = 0;
    // 作用：启动 GDI+ 图像解码与绘制服务。参数：无。返回：无；失败时令牌为 0。
    GdiPlusRuntime() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) token = 0;
    }
    // 作用：关闭本模块启动的 GDI+。参数：无。返回：无；启动失败时不执行清理。
    ~GdiPlusRuntime() { if (token) Gdiplus::GdiplusShutdown(token); }
};

// 作用：取得线程安全初始化的进程级 GDI+ 运行时。
// 参数：无。返回：运行时实例；图像对象必须在程序退出前释放。
GdiPlusRuntime& Runtime() {
    static GdiPlusRuntime runtime;
    return runtime;
}

// 作用：把显示器壁纸中卡片对应的屏幕区域绘制到客户区。
// 参数：dc 为目标设备上下文；window 为卡片窗口；client 为客户区；bounds 为所属显示器边界；
//       wallpaper 为已解码壁纸。返回：绘制成功为 true，缺失壁纸或图像无效时为 false。
bool DrawWallpaperCrop(HDC dc, HWND window, RECT client, RECT bounds,
                       Gdiplus::Bitmap* wallpaper) {
    if (!wallpaper || wallpaper->GetLastStatus() != Gdiplus::Ok ||
        !wallpaper->GetWidth() || !wallpaper->GetHeight()) return false;

    RECT screen{};
    if (!GetWindowRect(window, &screen)) return false;
    const float monitor_width = static_cast<float>(std::max(1L, bounds.right - bounds.left));
    const float monitor_height = static_cast<float>(std::max(1L, bounds.bottom - bounds.top));
    const float scale = std::max(monitor_width / wallpaper->GetWidth(),
                                 monitor_height / wallpaper->GetHeight());
    const float source_x = (screen.left - bounds.left) / scale +
        (wallpaper->GetWidth() - monitor_width / scale) / 2.0f;
    const float source_y = (screen.top - bounds.top) / scale +
        (wallpaper->GetHeight() - monitor_height / scale) / 2.0f;
    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    return graphics.DrawImage(wallpaper,
        Gdiplus::RectF(0.0f, 0.0f, static_cast<float>(client.right),
                       static_cast<float>(client.bottom)),
        source_x, source_y, static_cast<float>(client.right) / scale,
        static_cast<float>(client.bottom) / scale, Gdiplus::UnitPixel) == Gdiplus::Ok;
}

// 作用：在壁纸或回退底色上叠加玻璃白层并绘制圆角边缘。
// 参数：dc 为目标设备上下文；client 为客户区；dpi 为显示器 DPI。返回：无。
void DrawGlassWash(HDC dc, RECT client, UINT dpi) {
    HDC source = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    if (!source || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (source) DeleteDC(source);
        HBRUSH fallback = CreateSolidBrush(kFallbackSurface);
        FillRect(dc, &client, fallback);
        DeleteObject(fallback);
        return;
    }

    HGDIOBJ old_bitmap = SelectObject(source, bitmap);
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(source, &client, white);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, kGlassAlpha, 0};
    AlphaBlend(dc, 0, 0, client.right, client.bottom, source, 0, 0,
               client.right, client.bottom, blend);

    // 核心逻辑：不设置 WS_EX_LAYERED；这里只把白色像素混到壁纸底图，避免应用窗口画面被采入。
    HPEN border = CreatePen(PS_SOLID, std::max(1, platform::Scale(1, dpi)), kBorderColor);
    HGDIOBJ old_pen = SelectObject(dc, border);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 0, 0, client.right, client.bottom,
              platform::Scale(22, dpi), platform::Scale(22, dpi));
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(border);
    DeleteObject(white);
    SelectObject(source, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(source);
}

}  // 匿名命名空间结束

bool InitializeFenceRenderer() {
    return Runtime().token != 0;
}

void DrawFenceBackdrop(HDC dc, HWND window, RECT client, UINT dpi, RECT monitor_bounds,
                       Gdiplus::Bitmap* wallpaper) {
    if (!DrawWallpaperCrop(dc, window, client, monitor_bounds, wallpaper)) {
        HBRUSH fallback = CreateSolidBrush(kFallbackSurface);
        FillRect(dc, &client, fallback);
        DeleteObject(fallback);
    }
    DrawGlassWash(dc, client, dpi);
}

}  // desktop_orb::ui 命名空间结束
