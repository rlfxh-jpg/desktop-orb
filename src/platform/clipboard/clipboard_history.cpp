// 文件职责：监听调用方触发的剪贴板变化，复制文字或 DIB 像素到有界内存历史。
// 本模块不创建窗口；打开剪贴板的作用域由局部对象保证关闭。
#include "desktop_orb/platform/clipboard/clipboard_history.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <utility>

namespace desktop_orb::platform {
namespace {

constexpr size_t kMaximumEntries = 80;
constexpr size_t kMaximumImageBytes = 160U * 1024U * 1024U;
constexpr size_t kMaximumHistoryBytes = 256U * 1024U * 1024U;
constexpr size_t kMaximumTextLength = 100000;

struct ClipboardSession {
    bool opened = false;
    // 作用：打开进程共享的剪贴板。参数：无。返回：无；失败时 opened 为 false。
    ClipboardSession() : opened(OpenClipboard(nullptr) != FALSE) {}
    // 作用：关闭本对象成功打开的剪贴板。参数：无。返回：无。
    ~ClipboardSession() { if (opened) CloseClipboard(); }
};

// 作用：从剪贴板复制一个 DIB 内存块，并确认其宽高与内存上限。
// 参数：format 为 CF_DIB 或 CF_DIBV5；entry 接收像素与尺寸。
// 返回：读取到有效图片时为 true；格式、尺寸或内存不合法时为 false。
bool ReadDib(UINT format, ClipboardEntry& entry) {
    HGLOBAL handle = static_cast<HGLOBAL>(GetClipboardData(format));
    const SIZE_T bytes = handle ? GlobalSize(handle) : 0;
    if (bytes < sizeof(BITMAPINFOHEADER) || bytes > kMaximumImageBytes) return false;
    const void* memory = GlobalLock(handle);
    if (!memory) return false;
    BITMAPINFOHEADER header{};
    std::memcpy(&header, memory, sizeof(header));
    const bool valid = header.biSize >= sizeof(BITMAPINFOHEADER) &&
        header.biSize <= bytes &&
        header.biWidth > 0 && header.biHeight != 0 &&
        header.biWidth <= 16000 && header.biHeight > -16000 && header.biHeight < 16000 &&
        header.biPlanes == 1 && (header.biBitCount == 1 || header.biBitCount == 4 ||
            header.biBitCount == 8 || header.biBitCount == 16 ||
            header.biBitCount == 24 || header.biBitCount == 32);
    if (valid) {
        const auto* first = static_cast<const std::uint8_t*>(memory);
        entry.dib.assign(first, first + bytes);
        entry.image_format = format;
        entry.width = header.biWidth;
        entry.height = std::abs(header.biHeight);
    }
    GlobalUnlock(handle);
    return valid;
}

// 作用：把 CF_BITMAP 转成可独立保存和恢复的 32 位 DIB。
// 参数：entry 接收位图字节和尺寸。返回：转换成功为 true，否则为 false。
bool ReadBitmap(HBITMAP bitmap, ClipboardEntry& entry) {
    BITMAP details{};
    if (!bitmap || !GetObjectW(bitmap, sizeof(details), &details) ||
        details.bmWidth <= 0 || details.bmHeight <= 0 ||
        details.bmWidth > 16000 || details.bmHeight > 16000) return false;
    const size_t row = static_cast<size_t>(details.bmWidth) * 4;
    const size_t bytes = sizeof(BITMAPINFOHEADER) + row * details.bmHeight;
    if (bytes > kMaximumImageBytes) return false;
    entry.dib.resize(bytes);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = details.bmWidth;
    info.bmiHeader.biHeight = details.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    const int copied = dc ? GetDIBits(dc, bitmap, 0, details.bmHeight,
        entry.dib.data() + sizeof(BITMAPINFOHEADER), &info, DIB_RGB_COLORS) : 0;
    if (dc) ReleaseDC(nullptr, dc);
    if (!copied) { entry.dib.clear(); return false; }
    std::memcpy(entry.dib.data(), &info.bmiHeader, sizeof(BITMAPINFOHEADER));
    entry.width = details.bmWidth;
    entry.height = details.bmHeight;
    entry.image_format = CF_DIB;
    return true;
}

}  // 匿名命名空间结束

// 作用：把当前剪贴板文字或图片加入有界历史，重复内容只更新顺序。
// 参数：无。返回：记录列表变化时为 true，否则为 false。
bool ClipboardHistory::Capture() {
    ClipboardSession session;
    if (!session.opened) return false;
    ClipboardEntry entry;
    // 核心逻辑：图片优先于文字，避免含文字辅助格式的图片被误识别为纯文本。
    if ((!IsClipboardFormatAvailable(CF_DIBV5) || !ReadDib(CF_DIBV5, entry)) &&
        (!IsClipboardFormatAvailable(CF_DIB) || !ReadDib(CF_DIB, entry)) &&
        (!IsClipboardFormatAvailable(CF_BITMAP) ||
            !ReadBitmap(static_cast<HBITMAP>(GetClipboardData(CF_BITMAP)), entry))) {
        HGLOBAL handle = static_cast<HGLOBAL>(GetClipboardData(CF_UNICODETEXT));
        if (!handle) return false;
        const auto* data = static_cast<const wchar_t*>(GlobalLock(handle));
        if (!data) return false;
        const size_t limit = std::min(static_cast<size_t>(GlobalSize(handle) / sizeof(wchar_t)),
            kMaximumTextLength + 1);
        size_t length = 0;
        while (length < limit && data[length]) ++length;
        if (length < limit) entry.text.assign(data, length);
        GlobalUnlock(handle);
        if (entry.text.empty()) return false;
    }
    return Insert(std::move(entry));
}

// 作用：将保存后的独立截图位图加入历史，不改变用户当前剪贴板。
// 参数：bitmap 为调用方拥有的 HBITMAP。返回：尺寸有效且历史接收成功时为 true。
bool ClipboardHistory::AddBitmap(HBITMAP bitmap) {
    ClipboardEntry entry;
    return ReadBitmap(bitmap, entry) && Insert(std::move(entry));
}

// 作用：合并重复记录并淘汰最旧未置顶条目。
// 参数：entry 为新记录，内容在成功后转移到历史。返回：列表变化为 true。
bool ClipboardHistory::Insert(ClipboardEntry entry) {
    const auto existing = std::find_if(entries_.begin(), entries_.end(), [&entry](const auto& old) {
        return old.text == entry.text && old.dib == entry.dib;
    });
    if (existing != entries_.end()) {
        ClipboardEntry previous = std::move(*existing);
        entries_.erase(existing);
        entries_.insert(entries_.begin(), std::move(previous));
        return true;
    }
    entry.id = next_id_++;
    const auto inserted_id = entry.id;
    entries_.insert(entries_.begin(), std::move(entry));
    // 核心逻辑：多显示器截图可超过 24 MB；同时限制总占用，避免长时间运行耗尽内存。
    // 最旧的未置顶项先退出，已置顶项始终保留。
    size_t total_bytes = std::accumulate(entries_.begin(), entries_.end(), size_t{0},
        [](size_t bytes, const ClipboardEntry& item) {
            return bytes + item.dib.size() + item.text.size() * sizeof(wchar_t);
        });
    while (entries_.size() > kMaximumEntries || total_bytes > kMaximumHistoryBytes) {
        const auto removable = std::find_if(entries_.rbegin(), entries_.rend(),
            [](const auto& item) { return !item.pinned; });
        if (removable == entries_.rend()) break;
        total_bytes -= removable->dib.size() + removable->text.size() * sizeof(wchar_t);
        const bool removed_new_entry = removable->id == inserted_id;
        entries_.erase(std::next(removable).base());
        if (removed_new_entry) return false;
    }
    return true;
}

// 作用：重新发布历史文字或图片到 Windows 剪贴板。
// 参数：id 为记录标识。返回：系统接管内存时为 true，否则为 false。
bool ClipboardHistory::Restore(std::uint64_t id) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [id](const auto& item) { return item.id == id; });
    if (found == entries_.end()) return false;
    const bool image = !found->dib.empty();
    const SIZE_T bytes = image ? found->dib.size() : (found->text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    void* data = GlobalLock(memory);
    if (!data) { GlobalFree(memory); return false; }
    if (image) std::memcpy(data, found->dib.data(), bytes);
    else std::memcpy(data, found->text.c_str(), bytes);
    GlobalUnlock(memory);
    ClipboardSession session;
    if (!session.opened || !EmptyClipboard() ||
        !SetClipboardData(image ? found->image_format : CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

// 作用：反转指定历史记录的置顶状态。
// 参数：id 为记录标识。返回：找到记录时为 true。
bool ClipboardHistory::TogglePin(std::uint64_t id) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [id](const auto& item) { return item.id == id; });
    if (found == entries_.end()) return false;
    found->pinned = !found->pinned;
    return true;
}

// 作用：返回置顶优先且每组保持最近顺序的历史快照。
// 参数：无。返回：按显示顺序排列的借用指针，历史变化后失效。
std::vector<const ClipboardEntry*> ClipboardHistory::Entries() const {
    std::vector<const ClipboardEntry*> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) result.push_back(&entry);
    std::stable_partition(result.begin(), result.end(),
        [](const auto* entry) { return entry->pinned; });
    return result;
}

}  // desktop_orb::platform 命名空间结束
