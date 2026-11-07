#pragma once

// 文件职责：维护 DWM 实时缩略图注册与可见区域同步。
// 对象持有缩略图句柄，面板关闭或对象销毁时负责注销。
#include <windows.h>
#include <dwmapi.h>
#include <unordered_map>
#include <vector>

namespace desktop_orb::platform {

struct ThumbnailRequest {
    HWND source = nullptr;
    RECT bounds{};
    RECT clip{};
};

class ThumbnailManager {
public:
    // 作用：指定承载 DWM 缩略图的目标窗口。
    // 参数：destination 为已创建的面板 HWND。返回：无。
    explicit ThumbnailManager(HWND destination) : destination_(destination) {}
    // 作用：禁止复制 DWM 句柄集合。参数：另一对象。返回：无；接口已删除。
    ThumbnailManager(const ThumbnailManager&) = delete;
    // 作用：禁止复制赋值。参数：另一对象。返回：无；接口已删除。
    ThumbnailManager& operator=(const ThumbnailManager&) = delete;
    // 作用：注销仍由本对象持有的全部缩略图。参数：无。返回：无。
    ~ThumbnailManager();

    // 作用：只保留当前可见窗口的实时缩略图，并更新裁切与位置。
    // 参数：visible 为本次可见项及其目标/裁切矩形。返回：无；失败项可由 Failed 查询。
    void Sync(const std::vector<ThumbnailRequest>& visible);
    // 作用：注销并清空全部缩略图。参数：无。返回：无。
    void Clear();
    // 作用：查询指定窗口的缩略图注册或更新是否失败。
    // 参数：source 为源窗口句柄。返回：记录到失败时为 true，否则为 false。
    [[nodiscard]] bool Failed(HWND source) const;

private:
    struct Entry {
        HTHUMBNAIL thumbnail = nullptr;
        SIZE source_size{};
        bool failed = false;
    };
    HWND destination_;
    std::unordered_map<HWND, Entry> entries_;
};

}  // desktop_orb::platform 命名空间结束
