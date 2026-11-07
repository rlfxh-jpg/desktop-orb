#include "desktop_orb/ui/orb_artwork.hpp"

#include "../icon/resource.h"
#include <objidl.h>
#include <gdiplus.h>
#include <cstring>

namespace desktop_orb::ui {

struct OrbArtwork::Impl {
    ULONG_PTR gdiplus_token = 0;
    IStream* stream = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> image;

    Impl() {
        Gdiplus::GdiplusStartupInput startup;
        if (Gdiplus::GdiplusStartup(&gdiplus_token, &startup, nullptr) != Gdiplus::Ok) return;

        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_ORB_PNG), RT_RCDATA);
        if (!resource) return;
        const DWORD length = SizeofResource(module, resource);
        const void* bytes = LockResource(LoadResource(module, resource));
        if (!bytes || !length) return;

        HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, length);
        if (!buffer) return;
        void* destination = GlobalLock(buffer);
        if (!destination) {
            GlobalFree(buffer);
            return;
        }
        std::memcpy(destination, bytes, length);
        GlobalUnlock(buffer);
        // The stream owns the movable buffer. GDI+ keeps reading it until image destruction.
        if (CreateStreamOnHGlobal(buffer, TRUE, &stream) != S_OK) {
            GlobalFree(buffer);
            return;
        }
        image = std::make_unique<Gdiplus::Bitmap>(stream);
        if (image->GetLastStatus() != Gdiplus::Ok) image.reset();
    }

    ~Impl() {
        image.reset();
        if (stream) stream->Release();
        if (gdiplus_token) Gdiplus::GdiplusShutdown(gdiplus_token);
    }
};

OrbArtwork::OrbArtwork() : impl_(std::make_unique<Impl>()) {}
OrbArtwork::~OrbArtwork() = default;

bool OrbArtwork::ready() const { return impl_->image != nullptr; }

bool OrbArtwork::Render(HWND window, BYTE opacity) const {
    if (!ready() || !window) return false;
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return false;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return false;

    Gdiplus::Bitmap frame(width, height, PixelFormat32bppPARGB);
    if (frame.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics graphics(&frame);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (graphics.DrawImage(impl_->image.get(), 0, 0, width, height) != Gdiplus::Ok)
            return false;
    }

    BITMAPINFO info{};
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
    if (bitmap && pixels) {
        Gdiplus::BitmapData data{};
        Gdiplus::Rect area(0, 0, width, height);
        if (frame.LockBits(&area, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok) {
            for (int row = 0; row < height; ++row) {
                std::memcpy(static_cast<BYTE*>(pixels) + row * width * 4,
                            static_cast<const BYTE*>(data.Scan0) + row * data.Stride, width * 4);
            }
            frame.UnlockBits(&data);
            HGDIOBJ previous = SelectObject(memory, bitmap);
            POINT position{bounds.left, bounds.top};
            POINT origin{0, 0};
            SIZE size{width, height};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
            success = UpdateLayeredWindow(window, screen, &position, &size, memory,
                                          &origin, 0, &blend, ULW_ALPHA) != FALSE;
            SelectObject(memory, previous);
        }
    }
    // Restore selection before deleting the bitmap; layered windows copy these pixels.
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return success;
}

}  // namespace desktop_orb::ui
