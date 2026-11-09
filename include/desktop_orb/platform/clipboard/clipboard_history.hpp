#pragma once

// 文件职责：保存本次进程运行期间复制的文字与位图，并负责与 Windows 剪贴板交换数据。
// 历史记录只保存在内存中；HGLOBAL 由 Windows 剪贴板接管后不再由本模块释放。
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace desktop_orb::platform {

struct ClipboardEntry {
    std::uint64_t id = 0;
    std::wstring text;
    std::vector<std::uint8_t> dib;
    UINT image_format = 0;
    bool pinned = false;
    int width = 0;
    int height = 0;
};

class ClipboardHistory final {
public:
    // 作用：读取当前剪贴板中的 Unicode 文字或图片并加入进程内历史。
    // 参数：无。返回：历史发生变化为 true；格式不支持、重复或剪贴板占用时为 false。
    bool Capture();
    // 作用：将已生成的截图位图直接加入历史，不修改系统剪贴板。
    // 参数：bitmap 为调用方持有的有效位图；本方法不接管句柄。返回：加入成功为 true。
    bool AddBitmap(HBITMAP bitmap);
    // 作用：把指定历史条目重新发布到剪贴板。
    // 参数：id 为记录标识。返回：发布成功为 true；记录不存在或系统拒绝时为 false。
    bool Restore(std::uint64_t id) const;
    // 作用：切换记录置顶状态。参数：id 为记录标识。返回：找到记录为 true。
    bool TogglePin(std::uint64_t id);
    // 作用：取得按置顶优先及最近时间排序的记录快照。
    // 参数：无。返回：按显示顺序排列的借用指针；历史变化后调用方须重新取得。
    [[nodiscard]] std::vector<const ClipboardEntry*> Entries() const;

private:
    // 作用：去重、更新最近顺序并限制历史容量。
    // 参数：entry 为待加入且由本方法接管的记录。返回：列表变化时为 true。
    bool Insert(ClipboardEntry entry);
    std::vector<ClipboardEntry> entries_;
    std::uint64_t next_id_ = 1;
};

}  // desktop_orb::platform 命名空间结束
