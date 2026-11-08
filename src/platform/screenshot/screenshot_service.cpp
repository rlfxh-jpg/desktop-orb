// 文件职责：使用 Win32 GDI 捕获虚拟桌面，并将选区编码为 PNG 或发布到剪贴板。
// ScreenshotService 独占桌面位图；裁剪位图由单次保存/复制操作临时持有。
#include "desktop_orb/platform/screenshot/screenshot_service.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <algorithm>
#include <cwchar>
#include <utility>
#include <vector>

namespace desktop_orb::platform {
namespace {

// 作用：在离开作用域时关闭桌面或内存设备上下文。
// 参数：无。返回：无；is_screen 决定使用 ReleaseDC 还是 DeleteDC。
struct DeviceContext {
    HDC value = nullptr;
    bool is_screen = false;
    // 作用：释放由本结构持有的设备上下文。参数：无。返回：无。
    ~DeviceContext() {
        if (!value) return;
        if (is_screen) ReleaseDC(nullptr, value);
        else DeleteDC(value);
    }
};

// 作用：失败或提前返回时销毁未移交的位图句柄。
// 参数：无。返回：无；Release 可将所有权交给剪贴板。
struct BitmapHandle {
    HBITMAP value = nullptr;
    // 作用：接管新创建的位图句柄。参数：bitmap 为本对象独占的 HBITMAP。返回：无。
    explicit BitmapHandle(HBITMAP bitmap = nullptr) : value(bitmap) {}
    // 作用：禁止复制 GDI 句柄所有权。参数：other 为源对象。返回：无；接口已删除。
    BitmapHandle(const BitmapHandle&) = delete;
    // 作用：禁止复制赋值 GDI 句柄。参数：other 为源对象。返回：无；接口已删除。
    BitmapHandle& operator=(const BitmapHandle&) = delete;
    // 作用：转移位图句柄所有权。参数：other 为来源对象。返回：新所有者。
    BitmapHandle(BitmapHandle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    // 作用：转移赋值并释放原句柄。参数：other 为来源对象。返回：当前对象引用。
    BitmapHandle& operator=(BitmapHandle&& other) noexcept {
        if (this != &other) {
            if (value) DeleteObject(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }
    // 作用：释放当前拥有的 GDI 位图。参数：无。返回：无。
    ~BitmapHandle() { if (value) DeleteObject(value); }
    // 作用：放弃本地句柄所有权。参数：无。返回：原 HBITMAP。
    HBITMAP Release() { HBITMAP result = value; value = nullptr; return result; }
};

// 作用：查找 GDI+ PNG 编码器 CLSID。
// 参数：encoder 为接收 CLSID 的输出引用。返回：找到编码器为 true。
bool FindPngEncoder(CLSID& encoder) {
    UINT bytes = 0, count = 0;
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

// 作用：取得并创建用户图片目录下的 DesktopOrb 目录。
// 参数：无。返回：成功目录；系统查询或目录创建失败时为空路径。
std::filesystem::path ScreenshotDirectory() {
    PWSTR known_folder = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_CREATE, nullptr, &known_folder) != S_OK) return {};
    std::filesystem::path directory(known_folder);
    CoTaskMemFree(known_folder);
    directory /= L"DesktopOrb";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return error ? std::filesystem::path{} : directory;
}

// 作用：生成包含本地日期、时间和毫秒的 PNG 文件名。
// 参数：无。返回：可与截图目录组合的文件名。
std::wstring ScreenshotFilename() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[48]{};
    swprintf_s(name, L"Screenshot_%04u%02u%02u_%02u%02u%02u_%03u.png",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return name;
}

// 作用：启动 GDI+ 并在作用域结束时配对关闭。
// 参数：无。返回：初始化成功时 token 非零。
struct GdiPlusSession {
    ULONG_PTR token = 0;
    // 作用：初始化 GDI+。参数：无。返回：初始化成功为 true。
    bool Start() {
        Gdiplus::GdiplusStartupInput input;
        return Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    }
    // 作用：结束本对象启动的 GDI+ 会话。参数：无。返回：无。
    ~GdiPlusSession() { if (token) Gdiplus::GdiplusShutdown(token); }
};

// 作用：把位图保存为 PNG 文件。
// 参数：bitmap 为有效 GDI 位图；path 为完整输出路径；error 接收中文错误原因。
// 返回：保存成功为 true；编码器或文件写入失败时为 false。
bool SavePng(HBITMAP bitmap, const std::filesystem::path& path, std::wstring& error) {
    GdiPlusSession session;
    if (!session.Start()) { error = L"无法初始化 PNG 图片处理组件。"; return false; }
    Gdiplus::Bitmap image(bitmap, nullptr);
    CLSID encoder{};
    if (image.GetLastStatus() != Gdiplus::Ok || !FindPngEncoder(encoder)) {
        error = L"无法读取截图图像或找不到 PNG 编码器。";
        return false;
    }
    if (image.Save(path.c_str(), &encoder, nullptr) != Gdiplus::Ok) {
        error = L"无法将截图写入图片\\DesktopOrb 目录。";
        return false;
    }
    return true;
}

// 作用：将临时位图发布为剪贴板 CF_BITMAP。
// 参数：bitmap 为调用方拥有的位图句柄。返回：系统接管所有权时为 true。
bool PublishClipboard(BitmapHandle& bitmap) {
    bool opened = false;
    for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
        opened = OpenClipboard(nullptr) != FALSE;
        if (!opened) Sleep(20);
    }
    if (!opened) return false;
    const bool published = EmptyClipboard() && SetClipboardData(CF_BITMAP, bitmap.value) != nullptr;
    if (published) bitmap.Release();
    CloseClipboard();
    return published;
}

// 作用：创建截图指定矩形的独立 GDI 位图。
// 参数：source 为完整桌面位图；source_bounds 为其屏幕坐标范围；selection 为已校验选区。
// 返回：裁剪位图句柄；分配或 BitBlt 失败时为空。
BitmapHandle CropBitmap(HBITMAP source, RECT source_bounds, RECT selection) {
    const int width = selection.right - selection.left;
    const int height = selection.bottom - selection.top;
    DeviceContext screen{GetDC(nullptr), true};
    DeviceContext source_dc{screen.value ? CreateCompatibleDC(screen.value) : nullptr, false};
    DeviceContext target_dc{screen.value ? CreateCompatibleDC(screen.value) : nullptr, false};
    BitmapHandle cropped{target_dc.value ? CreateCompatibleBitmap(screen.value, width, height) : nullptr};
    if (!screen.value || !source_dc.value || !target_dc.value || !cropped.value) return BitmapHandle{};
    HGDIOBJ old_source = SelectObject(source_dc.value, source);
    HGDIOBJ old_target = SelectObject(target_dc.value, cropped.value);
    const bool valid = old_source && old_source != HGDI_ERROR && old_target && old_target != HGDI_ERROR;
    const bool copied = valid && BitBlt(target_dc.value, 0, 0, width, height, source_dc.value,
        selection.left - source_bounds.left, selection.top - source_bounds.top, SRCCOPY) != FALSE;
    if (old_source && old_source != HGDI_ERROR) SelectObject(source_dc.value, old_source);
    if (old_target && old_target != HGDI_ERROR) SelectObject(target_dc.value, old_target);
    if (!copied) { DeleteObject(cropped.value); cropped.value = nullptr; }
    return cropped;
}

}  // 匿名命名空间结束

ScreenshotService::~ScreenshotService() { Reset(); }

void ScreenshotService::Reset() {
    if (bitmap_) DeleteObject(bitmap_);
    bitmap_ = nullptr;
    bounds_ = {};
}

bool ScreenshotService::CaptureAllDisplays(std::wstring& error) {
    Reset();
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) { error = L"无法取得桌面显示区域尺寸。"; return false; }
    DeviceContext screen{GetDC(nullptr), true};
    DeviceContext memory{screen.value ? CreateCompatibleDC(screen.value) : nullptr, false};
    BitmapHandle captured{memory.value ? CreateCompatibleBitmap(screen.value, width, height) : nullptr};
    if (!screen.value || !memory.value || !captured.value) {
        error = L"无法分配桌面截图所需的图像资源。";
        return false;
    }
    HGDIOBJ previous = SelectObject(memory.value, captured.value);
    // 核心逻辑：多显示器虚拟桌面原点可能为负；源坐标必须使用系统给出的虚拟屏幕原点。
    const bool copied = previous && previous != HGDI_ERROR &&
        BitBlt(memory.value, 0, 0, width, height, screen.value, left, top, SRCCOPY | CAPTUREBLT) != FALSE;
    if (previous && previous != HGDI_ERROR) SelectObject(memory.value, previous);
    if (!copied) { error = L"Windows 未能读取所有显示器的桌面画面。"; return false; }
    bitmap_ = captured.Release();
    bounds_ = {left, top, left + width, top + height};
    error.clear();
    return true;
}

bool ScreenshotService::NormalizeSelection(RECT selection, RECT& cropped) const {
    if (!bitmap_) return false;
    if (selection.left > selection.right) std::swap(selection.left, selection.right);
    if (selection.top > selection.bottom) std::swap(selection.top, selection.bottom);
    cropped = {std::max(selection.left, bounds_.left), std::max(selection.top, bounds_.top),
               std::min(selection.right, bounds_.right), std::min(selection.bottom, bounds_.bottom)};
    return cropped.right > cropped.left && cropped.bottom > cropped.top;
}

ScreenshotResult ScreenshotService::SaveSelection(RECT selection) const {
    ScreenshotResult result;
    RECT clipped{};
    if (!NormalizeSelection(selection, clipped)) { result.error = L"请先框选有效的截图区域。"; return result; }
    const auto directory = ScreenshotDirectory();
    if (directory.empty()) { result.error = L"无法创建用户图片目录下的 DesktopOrb 文件夹。"; return result; }
    result.file_path = directory / ScreenshotFilename();
    auto cropped = CropBitmap(bitmap_, bounds_, clipped);
    if (!cropped.value) { result.error = L"无法裁剪所选截图区域。"; return result; }
    result.saved = SavePng(cropped.value, result.file_path, result.error);
    return result;
}

ScreenshotResult ScreenshotService::CopySelection(RECT selection) const {
    ScreenshotResult result;
    RECT clipped{};
    if (!NormalizeSelection(selection, clipped)) { result.error = L"请先框选有效的截图区域。"; return result; }
    auto cropped = CropBitmap(bitmap_, bounds_, clipped);
    if (!cropped.value) { result.error = L"无法裁剪所选截图区域。"; return result; }
    result.copied_to_clipboard = PublishClipboard(cropped);
    if (!result.copied_to_clipboard) result.error = L"无法打开剪贴板或发布截图。";
    return result;
}

}  // desktop_orb::platform 命名空间结束
