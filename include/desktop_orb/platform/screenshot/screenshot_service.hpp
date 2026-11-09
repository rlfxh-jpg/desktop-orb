#pragma once

// 文件职责：声明虚拟桌面截图快照的采集、裁剪保存和剪贴板发布接口。
// 模块由 platform/screenshot 实现；服务独占快照 HBITMAP，并在替换或析构时释放。
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include "desktop_orb/platform/screenshot/screenshot_edits.hpp"

namespace desktop_orb::platform {

// 作用：承载截图输出的保存路径和失败信息。
// 参数：无。返回：无；saved 或 copied_to_clipboard 分别表示对应操作是否成功。
struct ScreenshotResult {
    bool saved = false;
    bool copied_to_clipboard = false;
    std::filesystem::path file_path;
    std::wstring error;
};

// 作用：持有一次虚拟桌面快照，并对用户框选范围执行 PNG 保存或剪贴板复制。
// 参数：无。返回：无；快照所有权由对象独占管理。
class ScreenshotService final {
public:
    // 作用：释放仍由本服务持有的桌面快照。参数：无。返回：无。
    ~ScreenshotService();
    // 作用：禁止复制快照所有权。参数：other 为源对象。返回：无；接口已删除。
    ScreenshotService(const ScreenshotService&) = delete;
    // 作用：禁止复制赋值。参数：other 为源对象。返回：无；接口已删除。
    ScreenshotService& operator=(const ScreenshotService&) = delete;
    // 作用：创建尚未持有快照的服务。参数：无。返回：无。
    ScreenshotService() = default;

    // 作用：捕获所有显示器拼接成的虚拟桌面，并替换旧快照。
    // 参数：无。返回：成功为 true；系统无法读取桌面或分配图像资源时为 false。
    [[nodiscard]] bool CaptureAllDisplays(std::wstring& error);
    // 作用：查询快照覆盖的虚拟桌面屏幕坐标范围。
    // 参数：无。返回：屏幕坐标矩形；没有有效快照时为空矩形。
    [[nodiscard]] RECT bounds() const { return bounds_; }
    // 作用：查询是否持有有效桌面快照。参数：无。返回：有效为 true。
    [[nodiscard]] bool ready() const { return bitmap_ != nullptr; }
    // 作用：取得绘制选区背景所需的借用位图句柄。
    // 参数：无。返回：服务持有的 HBITMAP；调用方不得释放或修改。
    [[nodiscard]] HBITMAP bitmap() const { return bitmap_; }
    // 作用：把虚拟桌面选区裁剪后保存为 PNG。
    // 参数：selection 为屏幕坐标选区；directory 为用户选择的现有输出文件夹。
    // 返回：保存状态、输出路径及失败原因。
    [[nodiscard]] ScreenshotResult SaveSelection(
        RECT selection, const std::filesystem::path& directory,
        const ScreenshotEdits* edits = nullptr) const;
    // 作用：把虚拟桌面选区裁剪并编码为 PNG 字节，供截图分析模块上传。
    // 参数：selection 为屏幕坐标选区；png_bytes 接收完整 PNG；error 接收失败原因。
    // 返回：编码成功为 true；选区无效、裁剪失败或 PNG 编码失败时为 false。
    [[nodiscard]] bool EncodeSelectionPng(
        RECT selection, std::vector<std::uint8_t>& png_bytes, std::wstring& error,
        const ScreenshotEdits* edits = nullptr) const;
    // 作用：把虚拟桌面选区裁剪后复制到 Windows 剪贴板。
    // 参数：selection 为屏幕坐标选区。返回：复制成功状态及失败原因。
    [[nodiscard]] ScreenshotResult CopySelection(RECT selection, const ScreenshotEdits* edits = nullptr) const;
    // 作用：取得含编辑内容的独立选区位图，供桌面贴图窗口长期持有。
    // 参数：selection 为屏幕选区；edits 为可选编辑数据；error 接收错误。
    // 返回：调用方拥有的 HBITMAP；失败时为空，调用方须用 DeleteObject 释放非空句柄。
    [[nodiscard]] HBITMAP CreateSelectionBitmap(RECT selection, const ScreenshotEdits* edits,
        std::wstring& error) const;
    // 作用：销毁当前快照并清空坐标范围。参数：无。返回：无。
    void Reset();

private:
    // 作用：校验并裁剪屏幕坐标选区到快照范围。
    // 参数：selection 为屏幕坐标选区；cropped 为接收裁剪后坐标的输出引用。
    // 返回：交集宽高均大于零且快照有效时为 true。
    [[nodiscard]] bool NormalizeSelection(RECT selection, RECT& cropped) const;
    HBITMAP bitmap_ = nullptr;
    RECT bounds_{};
};

}  // desktop_orb::platform 命名空间结束
