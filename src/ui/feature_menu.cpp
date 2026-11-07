// 文件职责：创建环绕悬浮球的图标按钮，负责悬停缩放、布局、绘制及延迟显示名称提示。
// 菜单持有按钮；每个按钮持有自己的 Tooltip 窗口，业务动作由调用方回调执行。
#include "desktop_orb/ui/feature_menu.hpp"

#include "desktop_orb/ui/drawing.hpp"
#include "desktop_orb/ui/feature_layout.hpp"
#include "desktop_orb/ui/icon_image.hpp"
#include <algorithm>
#include <commctrl.h>

namespace desktop_orb::ui {

constexpr WORD kTooltipDelayMs = 3000;

class FeatureMenu::Button final : public WindowBase {
public:
    // 作用：保存按钮提示文字和点击动作。
    // 参数：title 为悬停提示文字；icon_filename 为 PNG 文件名；action 为点击回调。
    // 返回：无；图标加载失败时绘制会退回到简易窗口符号。
    Button(std::wstring title, const wchar_t* icon_filename, std::function<void()> action)
        : title_(std::move(title)), icon_(icon_filename), action_(std::move(action)) {}

    // 作用：销毁按钮持有的 Tooltip，按钮 HWND 随后由基类销毁。
    // 参数：无。返回：无。
    ~Button() override {
        if (tooltip_) DestroyWindow(tooltip_);
    }

    // 作用：创建初始隐藏的功能按钮窗口，并初始化透明图像与名称提示。
    // 参数：无。返回：按钮及提示均创建成功为 true，Win32 创建失败为 false。
    bool Create() {
        // 核心逻辑：有 PNG 时使用逐像素透明的分层窗口；缺图时仍绘制普通圆形备用符号。
        const DWORD ex_style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
            (icon_.ready() ? WS_EX_LAYERED : 0);
        if (!CreateNative(L"DesktopOrbFeature", title_.c_str(), WS_POPUP,
                          ex_style,
                          0, 0, kDefaultFeatureDiameter, kDefaultFeatureDiameter)) return false;
        if (icon_.ready()) {
            if (!icon_.RenderLayered(handle(), 255)) return false;
        } else {
            SetWindowRgn(handle(), CreateEllipticRgn(0, 0, kDefaultFeatureDiameter, kDefaultFeatureDiameter), TRUE);
        }

        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        if (!InitCommonControlsEx(&controls)) return false;
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                   WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                   handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!tooltip_) return false;

        // 核心逻辑：系统默认控件版本只接受旧版结构体大小；Tooltip 子类化按钮接收鼠标消息。
        // 提示文本指向按钮持有的字符串，按钮析构前必须先销毁 Tooltip。
        TOOLINFOW tool{};
        tool.cbSize = TTTOOLINFOW_V1_SIZE;
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = handle();
        tool.uId = reinterpret_cast<UINT_PTR>(handle());
        tool.lpszText = title_.data();
        if (!SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) return false;
        // 核心逻辑：初次进入和再次进入按钮都需完整悬停三秒，避免快速移出移入时立刻显示。
        SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELONG(kTooltipDelayMs, 0));
        SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELONG(kTooltipDelayMs, 0));
        return true;
    }

    // 作用：设置按钮中心及默认、悬停直径，并以默认大小显示。
    // 参数：center_x、center_y 为屏幕中心坐标；default_size、hovered_size 为物理像素直径。
    // 返回：无。
    void Position(int center_x, int center_y, int default_size, int hovered_size) {
        center_x_ = center_x;
        center_y_ = center_y;
        default_size_ = default_size;
        hovered_size_ = hovered_size;
        expanded_ = false;
        SetDiameter(default_size_);
    }

    // 作用：取消鼠标离开跟踪并恢复默认直径，供菜单隐藏时调用。
    // 参数：无。返回：无。
    void ResetHover() {
        if (tracking_mouse_) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE | TME_CANCEL, handle(), 0};
            TrackMouseEvent(&tracking);
            tracking_mouse_ = false;
        }
        expanded_ = false;
        SetDiameter(default_size_);
    }

    // 作用：启用提示，供菜单重新展开时调用。
    // 参数：无。返回：无。
    void EnableTooltip() {
        if (tooltip_) SendMessageW(tooltip_, TTM_ACTIVATE, TRUE, 0);
    }

    // 作用：关闭提示并停用等待中的显示计时，供菜单收起时调用。
    // 参数：无。返回：无。
    void DismissTooltip() {
        if (tooltip_) SendMessageW(tooltip_, TTM_ACTIVATE, FALSE, 0);
    }

protected:
    // 作用：处理按钮绘制与点击，其他消息交给基类。
    // 参数：message 为消息码；wparam、lparam 为消息数据。
    // 返回：已处理时为 0，否则为基类消息处理结果。
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_MOUSEMOVE) {
            if (!tracking_mouse_) {
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, handle(), 0};
                tracking_mouse_ = TrackMouseEvent(&tracking) != FALSE;
            }
            if (!expanded_) {
                expanded_ = true;
                SetDiameter(hovered_size_);
            }
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            tracking_mouse_ = false;
            expanded_ = false;
            SetDiameter(default_size_);
            return 0;
        }
        if (message == WM_PAINT) {
            if (icon_.ready()) {
                // 核心逻辑：分层窗口的图像由 UpdateLayeredWindow 提交，不在 WM_PAINT 中叠加不透明背景。
                ValidateRect(handle(), nullptr);
                return 0;
            }
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(handle(), &paint);
            RECT bounds{};
            GetClientRect(handle(), &bounds);
            const int size = bounds.right;
            Fill(dc, bounds, Color(36, 52, 57));
            HBRUSH transparent = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            HGDIOBJ old_brush = SelectObject(dc, transparent);
            HPEN pen = CreatePen(PS_SOLID, std::max(1, size / 41), Color(84, 211, 184));
            HGDIOBJ old_pen = SelectObject(dc, pen);
            Rectangle(dc, size * 34 / 100, size * 34 / 100, size * 66 / 100, size * 59 / 100);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
            Line(dc, {size / 2, size * 59 / 100}, {size / 2, size * 67 / 100}, Color(84, 211, 184), 2);
            Line(dc, {size * 39 / 100, size * 67 / 100}, {size * 61 / 100, size * 67 / 100}, Color(84, 211, 184), 2);
            EndPaint(handle(), &paint);
            return 0;
        }
        if (message == WM_LBUTTONUP) {
            if (action_) action_();
            return 0;
        }
        return WindowBase::HandleMessage(message, wparam, lparam);
    }

private:
    // 作用：围绕固定中心调整窗口尺寸，并同步更新透明图像或备用圆形点击区域。
    // 参数：diameter 为目标物理像素直径。返回：无。
    void SetDiameter(int diameter) {
        SetWindowPos(handle(), nullptr, center_x_ - diameter / 2, center_y_ - diameter / 2,
                     diameter, diameter, SWP_NOACTIVATE | SWP_NOZORDER);
        if (icon_.ready()) {
            icon_.RenderLayered(handle(), 255);
        } else {
            SetWindowRgn(handle(), CreateEllipticRgn(0, 0, diameter, diameter), TRUE);
        }
    }

    std::wstring title_;
    IconImage icon_;
    std::function<void()> action_;
    HWND tooltip_ = nullptr;
    int center_x_ = 0;
    int center_y_ = 0;
    int default_size_ = kDefaultFeatureDiameter;
    int hovered_size_ = kHoveredFeatureDiameter;
    bool tracking_mouse_ = false;
    bool expanded_ = false;
};

FeatureMenu::FeatureMenu() = default;
FeatureMenu::~FeatureMenu() = default;

bool FeatureMenu::Add(std::wstring title, const wchar_t* icon_filename, std::function<void()> action) {
    auto button = std::make_unique<Button>(std::move(title), icon_filename, std::move(action));
    if (!button->Create()) return false;
    buttons_.push_back(std::move(button));
    return true;
}

void FeatureMenu::ShowAround(RECT orb, const platform::Monitor& monitor, UINT dpi) {
    const FeatureLayout layout = ArrangeFeatures(orb, monitor.work, dpi, buttons_.size());
    if (layout.centers.empty() || layout.centers.size() != buttons_.size()) return;
    for (size_t index = 0; index < buttons_.size(); ++index) {
        buttons_[index]->Position(layout.centers[index].x, layout.centers[index].y,
                                  layout.default_diameter, layout.hovered_diameter);
        buttons_[index]->EnableTooltip();
        ShowWindow(buttons_[index]->handle(), SW_SHOWNOACTIVATE);
    }
    visible_ = true;
}

void FeatureMenu::Hide() {
    if (!visible_) return;
    for (const auto& button : buttons_) {
        button->DismissTooltip();
        ShowWindow(button->handle(), SW_HIDE);
        button->ResetHover();
    }
    visible_ = false;
}

std::vector<HWND> FeatureMenu::Handles() const {
    std::vector<HWND> handles;
    for (const auto& button : buttons_) handles.push_back(button->handle());
    return handles;
}

}  // desktop_orb::ui 命名空间结束
