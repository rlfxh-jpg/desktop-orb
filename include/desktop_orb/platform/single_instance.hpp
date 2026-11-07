#pragma once

#include <windows.h>

namespace desktop_orb::platform {

class SingleInstance {
public:
    enum class Result { acquired, already_running, error };

    SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    ~SingleInstance();

    [[nodiscard]] Result result() const { return result_; }
    [[nodiscard]] DWORD error_code() const { return error_code_; }

private:
    HANDLE mutex_ = nullptr;
    Result result_ = Result::error;
    DWORD error_code_ = ERROR_SUCCESS;
};

}  // namespace desktop_orb::platform
