#pragma once

// 文件职责：定义 UI 层的 PNG 图标接口，统一加载、绘制和透明像素输出。
// 图标对象持有解码结果；普通窗口和分层窗口共用这一接口，绘制时临时持有 GDI 资源。
#include <memory>
#include <windows.h>

namespace desktop_orb::ui {

// 生命周期：作为界面对象的成员保存，不在每次 WM_PAINT 时重新读取文件。
class IconImage final {
public:
    // 作用：从 assets/icons 加载并解码一个 PNG 图标。
    // 参数：filename 为图标文件名，不含目录；空值、缺失或无效图片将使 ready() 为 false。
    // 返回：无；构造失败不抛出资源错误，由调用方检查 ready() 并选择降级行为。
    explicit IconImage(const wchar_t* filename);
    // 作用：释放当前对象持有的解码图片，早于共享 GDI+ 运行时关闭。
    // 参数：无。返回：无。
    ~IconImage();
    // 作用：禁止复制持有图片资源的对象，避免所有权不明确。
    // 参数：另一 IconImage 对象。返回：无；复制接口已删除。
    IconImage(const IconImage&) = delete;
    // 作用：禁止复制赋值，保持图片资源唯一所有权。
    // 参数：另一 IconImage 对象。返回：无；赋值接口已删除。
    IconImage& operator=(const IconImage&) = delete;

    // 作用：检查 PNG 是否已成功解码。
    // 参数：无。返回：成功时为 true，缺图、格式错误或 GDI+ 初始化失败时为 false。
    [[nodiscard]] bool ready() const;
    // 作用：在普通窗口的设备上下文中按目标矩形绘制图标，并保留透明边缘。
    // 参数：dc 为目标设备上下文；bounds 为以设备像素表示的绘制区域。
    // 返回：成功为 true；设备上下文、区域或图片无效，以及 GDI+ 失败时为 false。
    bool Draw(HDC dc, RECT bounds) const;
    // 作用：为 UpdateLayeredWindow 输出从上到下排列的预乘 Alpha BGRA 像素。
    // 参数：width、height 为目标像素尺寸；pixels 为调用方分配的缓冲区；
    //       stride 为每行字节数，至少为 width * 4，缓冲区须容纳 height 行。
    // 返回：成功为 true；尺寸、缓冲区或图片无效以及绘制失败时为 false。
    bool Rasterize(int width, int height, void* pixels, int stride) const;
    // 作用：把图标按窗口尺寸提交到分层窗口，保留 PNG 逐像素透明度。
    // 参数：window 为已创建的分层窗口；opacity 为叠加在 PNG Alpha 上的整体透明度。
    // 返回：提交成功为 true；窗口、图片或 GDI 资源不可用时为 false。
    bool RenderLayered(HWND window, BYTE opacity) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // desktop_orb::ui 命名空间结束
