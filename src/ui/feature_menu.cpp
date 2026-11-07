#include "desktop_orb/ui/feature_menu.hpp"

#include "desktop_orb/ui/drawing.hpp"
#include <algorithm>

namespace desktop_orb::ui {

class FeatureMenu::Button final : public WindowBase {
public:
    Button(std::wstring title, std::function<void()> action)
        : title_(std::move(title)), action_(std::move(action)) {}

    bool Create() {
        if (!CreateNative(L"DesktopOrbFeature", title_.c_str(), WS_POPUP,
                          WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                          0, 0, 82, 82)) return false;
        SetWindowRgn(handle(), CreateEllipticRgn(0, 0, 82, 82), TRUE);
        return true;
    }

    void Position(int x, int y, int size) {
        SetWindowPos(handle(), HWND_TOPMOST, x, y, size, size, SWP_NOACTIVATE);
        SetWindowRgn(handle(), CreateEllipticRgn(0, 0, size, size), TRUE);
    }

protected:
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_PAINT) {
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
            Rectangle(dc, size * 34 / 100, size * 22 / 100, size * 66 / 100, size * 47 / 100);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
            Line(dc, {size / 2, size * 47 / 100}, {size / 2, size * 55 / 100}, Color(84, 211, 184), 2);
            Line(dc, {size * 39 / 100, size * 55 / 100}, {size * 61 / 100, size * 55 / 100}, Color(84, 211, 184), 2);
            Text(dc, title_, {3, size * 58 / 100, size - 3, size * 85 / 100},
                 std::max(11, size * 12 / 82), Color(244, 251, 249), FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    std::wstring title_;
    std::function<void()> action_;
};

FeatureMenu::FeatureMenu() = default;
FeatureMenu::~FeatureMenu() = default;

bool FeatureMenu::Add(std::wstring title, std::function<void()> action) {
    auto button = std::make_unique<Button>(std::move(title), std::move(action));
    if (!button->Create()) return false;
    buttons_.push_back(std::move(button));
    return true;
}

void FeatureMenu::ShowAround(RECT orb, const platform::Monitor& monitor, UINT dpi) {
    const int size = platform::Scale(82, dpi);
    const int gap = platform::Scale(24, dpi);
    const int center_x = (orb.left + orb.right) / 2;
    const int center_y = (orb.top + orb.bottom) / 2;
    const int direction = center_x < (monitor.work.left + monitor.work.right) / 2 ? 1 : -1;
    for (size_t index = 0; index < buttons_.size(); ++index) {
        const int offset = (static_cast<int>(index) * 2 - static_cast<int>(buttons_.size()) + 1) * (size + gap / 2) / 2;
        const int x = std::clamp(center_x + direction * (static_cast<int>(orb.right - orb.left) / 2 + gap + size / 2) - size / 2,
                                 static_cast<int>(monitor.work.left), static_cast<int>(monitor.work.right - size));
        const int y = std::clamp(center_y + offset - size / 2, static_cast<int>(monitor.work.top),
                                 static_cast<int>(monitor.work.bottom - size));
        buttons_[index]->Position(x, y, size);
        ShowWindow(buttons_[index]->handle(), SW_SHOWNOACTIVATE);
    }
    visible_ = true;
}

void FeatureMenu::Hide() {
    if (!visible_) return;
    for (const auto& button : buttons_) ShowWindow(button->handle(), SW_HIDE);
    visible_ = false;
}

std::vector<HWND> FeatureMenu::Handles() const {
    std::vector<HWND> handles;
    for (const auto& button : buttons_) handles.push_back(button->handle());
    return handles;
}

}  // namespace desktop_orb::ui
