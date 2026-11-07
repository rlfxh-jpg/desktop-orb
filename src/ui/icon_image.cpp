// 文件职责：从部署目录加载 PNG，并集中管理 GDI+ 初始化和图片解码资源。
// 普通窗口绘制与分层窗口像素输出共用同一份解码结果；分层绘制的 GDI 资源即用即释放。
#include "desktop_orb/ui/icon_image.hpp"

#include "desktop_orb/platform/asset_path.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <climits>
#include <cstring>
#include <string>

namespace desktop_orb::ui {
namespace {

struct GdiplusRuntime {
    ULONG_PTR token = 0;

    // 作用：在首次加载图片前启动 GDI+。
    // 参数：无。返回：无；失败时 token 保持为 0，后续加载会返回不可用状态。
    GdiplusRuntime() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
            token = 0;
    }
    // 作用：在所有图标对象销毁后关闭 GDI+。
    // 参数：无。返回：无；未成功启动时无需清理。
    ~GdiplusRuntime() {
        if (token) Gdiplus::GdiplusShutdown(token);
    }
};

// 作用：取得进程共享的 GDI+ 运行时，避免每张图重复初始化。
// 参数：无。返回：运行时对象引用；局部静态初始化具备线程安全性。
GdiplusRuntime& Runtime() {
    static GdiplusRuntime runtime;
    return runtime;
}

}  // 匿名命名空间结束

// 实现细节：只持有解码后的 Bitmap；路径计算和解码失败由构造函数处理。
struct IconImage::Impl {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;

    // 作用：按文件名加载一次 PNG，并保存解码结果。
    // 参数：filename 为 assets/icons 下的文件名。返回：无；失败时 bitmap 为空。
    // 核心约束：GDI+ 可能持续占用源文件，替换图片需重启程序后生效。
    explicit Impl(const wchar_t* filename) {
        if (!filename || !*filename || !Runtime().token) return;
        std::wstring relative = L"icons/";
        relative += filename;
        const auto path = platform::AssetPath(relative);
        if (path.empty()) return;
        bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str());
        if (bitmap->GetLastStatus() != Gdiplus::Ok) bitmap.reset();
    }
};

IconImage::IconImage(const wchar_t* filename) : impl_(std::make_unique<Impl>(filename)) {}
IconImage::~IconImage() = default;

bool IconImage::ready() const { return impl_->bitmap != nullptr; }

bool IconImage::Draw(HDC dc, RECT bounds) const {
    if (!ready() || !dc || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return false;
    Gdiplus::Graphics graphics(dc);
    // 核心逻辑：绘制时再按像素区域缩放，使同一 PNG 适配不同 DPI。
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    return graphics.DrawImage(impl_->bitmap.get(), static_cast<int>(bounds.left),
                              static_cast<int>(bounds.top),
                              static_cast<int>(bounds.right - bounds.left),
                              static_cast<int>(bounds.bottom - bounds.top)) == Gdiplus::Ok;
}

bool IconImage::Rasterize(int width, int height, void* pixels, int stride) const {
    if (!ready() || !pixels || width <= 0 || height <= 0 || width > INT_MAX / 4 ||
        stride < width * 4) return false;
    Gdiplus::Bitmap frame(width, height, PixelFormat32bppPARGB);
    if (frame.GetLastStatus() != Gdiplus::Ok) return false;
    // 核心逻辑：UpdateLayeredWindow 要求颜色通道按 Alpha 预乘；
    // 先绘制到 PARGB 位图完成转换，同时保留 PNG 的透明边缘。
    {
        Gdiplus::Graphics graphics(&frame);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (graphics.DrawImage(impl_->bitmap.get(), 0, 0, width, height) != Gdiplus::Ok)
            return false;
    }
    Gdiplus::BitmapData data{};
    Gdiplus::Rect area(0, 0, width, height);
    // 核心约束：结束 Graphics 绘制后才能锁定位图内存，避免 GDI+ 对象忙碌。
    if (frame.LockBits(&area, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB,
                       &data) != Gdiplus::Ok) return false;
    for (int row = 0; row < height; ++row) {
        std::memcpy(static_cast<BYTE*>(pixels) + row * stride,
                    static_cast<const BYTE*>(data.Scan0) + row * data.Stride, width * 4);
    }
    return frame.UnlockBits(&data) == Gdiplus::Ok;
}

bool IconImage::RenderLayered(HWND window, BYTE opacity) const {
    if (!ready() || !window) return false;
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return false;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return false;

    BITMAPINFO info{};
    // 核心逻辑：负高度让 DIB 自上而下排列，与 Rasterize 的预乘 Alpha BGRA 输出一致。
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC memory = CreateCompatibleDC(screen);
    void* pixels = nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    bool success = false;
    if (bitmap && pixels && Rasterize(width, height, pixels, width * 4)) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        POINT position{bounds.left, bounds.top};
        POINT origin{0, 0};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
        success = UpdateLayeredWindow(window, screen, &position, &size, memory,
                                      &origin, 0, &blend, ULW_ALPHA) != FALSE;
        SelectObject(memory, previous);
    }
    // 核心逻辑：UpdateLayeredWindow 会复制像素，成功和失败路径均可立即释放临时 GDI 资源。
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return success;
}

}  // desktop_orb::ui 命名空间结束
