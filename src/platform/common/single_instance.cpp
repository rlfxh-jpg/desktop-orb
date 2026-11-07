// 文件职责：通过全局命名互斥锁检查是否已有悬浮球实例。
// 成功获取的 HANDLE 由 SingleInstance 持有，并在析构时关闭。
#include "desktop_orb/platform/common/single_instance.hpp"

namespace desktop_orb::platform {
namespace {
constexpr wchar_t kMutexName[] = L"Global\\DesktopOrb-8D0FC1B9-7304-4D5A-95E8-BB3A72C612FD";
}

SingleInstance::SingleInstance() {
    mutex_ = CreateMutexW(nullptr, FALSE, kMutexName);
    error_code_ = GetLastError();
    if (mutex_) {
        if (error_code_ == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
            result_ = Result::already_running;
        } else {
            result_ = Result::acquired;
        }
    } else if (error_code_ == ERROR_ACCESS_DENIED) {
        // 核心逻辑：其他登录会话可能已持有全局互斥锁，但拒绝当前会话访问。
        result_ = Result::already_running;
    }
}

SingleInstance::~SingleInstance() {
    if (mutex_) CloseHandle(mutex_);
}

}  // desktop_orb::platform 命名空间结束
