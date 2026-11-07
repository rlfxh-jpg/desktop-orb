#include "desktop_orb/platform/single_instance.hpp"

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
        // Another user/session may own the global mutex without granting access.
        result_ = Result::already_running;
    }
}

SingleInstance::~SingleInstance() {
    if (mutex_) CloseHandle(mutex_);
}

}  // namespace desktop_orb::platform
