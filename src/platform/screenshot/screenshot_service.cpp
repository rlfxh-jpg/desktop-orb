// 文件职责：使用 Win32 GDI 捕获所有显示器组成的虚拟桌面，并以 GDI+ 编码 PNG。
// 文件、设备上下文、位图和 GDI+ 运行时均在单次调用内管理；成功交给剪贴板的位图由系统接管。
#include "desktop_orb/platform/screenshot/screenshot_service.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

namespace desktop_orb::platform {
namespace {

// 作用：释放截图期间创建的设备上下文。
// 参数：无。返回：无；仅当句柄有效时释放对应 GDI 资源。
struct DeviceContext {
    HDC value = nullptr;
    bool is_screen = false;

    // 作用：根据来源释放设备上下文。参数：无。返回：无。
    ~DeviceContext() {
        if (!value) return;
        if (is_screen) ReleaseDC(nullptr, value);
        else DeleteDC(value);
    }
};

// 作用：在失败或提前返回时销毁尚未移交剪贴板的位图。
// 参数：无。返回：无；Release 可将所有权转交给剪贴板。
struct BitmapHandle {
    HBITMAP value = nullptr;

    // 作用：释放当前拥有的位图句柄。参数：无。返回：无。
    ~BitmapHandle() { if (value) DeleteObject(value); }

    // 作用：放弃本地所有权以交给剪贴板。参数：无。返回：被移交的 HBITMAP。
    HBITMAP Release() {
        HBITMAP result = value;
        value = nullptr;
        return result;
    }
};

// 作用：从 GDI+ 编码器列表中查找 PNG 编码器。
// 参数：encoder 为接收编码器 CLSID 的输出引用。返回：找到 PNG 编码器时为 true。
bool FindPngEncoder(CLSID& encoder) {
    UINT bytes = 0;
    UINT count = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return false;
    std::vector<BYTE> storage(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return false;
    for (UINT index = 0; index < count; ++index) {
        if (std::wcscmp(encoders[index].MimeType, L"image/png") == 0) {
            encoder = encoders[index].Clsid;
            return true;
        }
    }
    return false;
}

// 作用：确定用户图片目录下 DesktopOrb 子目录的路径。
// 参数：无。返回：成功取得并创建的目录；系统目录查询或创建失败时返回空路径。
std::filesystem::path ScreenshotDirectory() {
    PWSTR known_folder = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_CREATE, nullptr, &known_folder) != S_OK)
        return {};
    std::filesystem::path directory(known_folder);
    CoTaskMemFree(known_folder);
    directory /= L"DesktopOrb";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return error ? std::filesystem::path{} : directory;
}

// 作用：生成包含本地日期时间和毫秒的 PNG 文件名。
// 参数：无。返回：适合与截图目录组合的文件名。
std::wstring ScreenshotFilename() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[48]{};
    swprintf_s(name, L"Screenshot_%04u%02u%02u_%02u%02u%02u_%03u.png",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return name;
}

// 作用：把捕获位图保存为 PNG 文件。
// 参数：bitmap 为有效 GDI 位图；path 为完整输出路径；error 为接收中文失败原因的引用。
// 返回：文件成功写入为 true；编码器、GDI+ 或文件写入失败时为 false。
bool SavePng(HBITMAP bitmap, const std::filesystem::path& path, std::wstring& error) {
    Gdiplus::Bitmap image(bitmap, nullptr);
    if (image.GetLastStatus() != Gdiplus::Ok) {
        error = L"无法读取桌面截图图像。";
        return false;
    }
    CLSID encoder{};
    if (!FindPngEncoder(encoder)) {
        error = L"系统中未找到 PNG 图片编码器。";
        return false;
    }
    if (image.Save(path.c_str(), &encoder, nullptr) != Gdiplus::Ok) {
        error = L"无法将截图写入图片\\DesktopOrb 目录。";
        return false;
    }
    return true;
}

// 作用：将桌面位图发布为 Windows 剪贴板的位图格式。
// 参数：bitmap 为调用方拥有的有效位图句柄。返回：系统接管位图所有权时为 true。
bool CopyBitmapToClipboard(BitmapHandle& bitmap) {
    bool opened = false;
    for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
        opened = OpenClipboard(nullptr) != FALSE;
        if (!opened) Sleep(20);
    }
    if (!opened) return false;
    const bool emptied = EmptyClipboard() != FALSE;
    const bool published = emptied && SetClipboardData(CF_BITMAP, bitmap.value) != nullptr;
    if (published) bitmap.Release();
    CloseClipboard();
    return published;
}

}  // 匿名命名空间结束

ScreenshotResult ScreenshotService::CaptureAllDisplays() const {
    ScreenshotResult result;
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        result.error = L"无法取得桌面显示区域尺寸。";
        return result;
    }

    const auto directory = ScreenshotDirectory();
    if (directory.empty()) {
        result.error = L"无法创建用户图片目录下的 DesktopOrb 文件夹。";
        return result;
    }
    result.file_path = directory / ScreenshotFilename();

    Gdiplus::GdiplusStartupInput startup_input;
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &startup_input, nullptr) != Gdiplus::Ok) {
        result.error = L"无法初始化 PNG 图片处理组件。";
        return result;
    }

    DeviceContext screen{GetDC(nullptr), true};
    DeviceContext memory{screen.value ? CreateCompatibleDC(screen.value) : nullptr, false};
    BitmapHandle bitmap{memory.value ? CreateCompatibleBitmap(screen.value, width, height) : nullptr};
    if (!screen.value || !memory.value || !bitmap.value) {
        result.error = L"无法分配桌面截图所需的图像资源。";
    } else {
        HGDIOBJ previous = SelectObject(memory.value, bitmap.value);
        // 核心逻辑：虚拟屏幕原点可能为负值；CAPTUREBLT 同时包含常见分层窗口画面。
        const bool captured = previous && previous != HGDI_ERROR &&
            BitBlt(memory.value, 0, 0, width, height, screen.value, left, top,
                   SRCCOPY | CAPTUREBLT) != FALSE;
        if (previous && previous != HGDI_ERROR) SelectObject(memory.value, previous);
        // 核心约束：先从内存设备上下文中解除位图选择，再交给 GDI+ 编码并交给剪贴板。
        if (captured) result.saved = SavePng(bitmap.value, result.file_path, result.error);
        else result.error = L"Windows 未能读取所有显示器的桌面画面。";
    }

    if (result.saved) result.copied_to_clipboard = CopyBitmapToClipboard(bitmap);
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return result;
}

}  // desktop_orb::platform 命名空间结束
