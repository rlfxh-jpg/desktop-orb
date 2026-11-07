#pragma once

// 文件职责：使用全局命名互斥锁保证系统内只运行一个悬浮球实例。
// 对象持有成功创建的互斥锁句柄，并在析构时释放。
#include <windows.h>

namespace desktop_orb::platform {

class SingleInstance {
public:
    enum class Result { acquired, already_running, error };

    // 作用：尝试获取全局单实例锁。参数：无。返回：无；结果通过 result() 查询。
    SingleInstance();
    // 作用：禁止复制互斥锁句柄。参数：另一对象。返回：无；复制接口已删除。
    SingleInstance(const SingleInstance&) = delete;
    // 作用：禁止复制赋值。参数：另一对象。返回：无；赋值接口已删除。
    SingleInstance& operator=(const SingleInstance&) = delete;
    // 作用：释放当前实例持有的互斥锁句柄。参数：无。返回：无。
    ~SingleInstance();

    // 作用：读取单实例检查结果。参数：无。
    // 返回：已获取锁、已有实例或检查出错三种状态之一。
    [[nodiscard]] Result result() const { return result_; }
    // 作用：读取创建互斥锁时的 Windows 错误码。参数：无。
    // 返回：原始错误码，供失败诊断使用。
    [[nodiscard]] DWORD error_code() const { return error_code_; }

private:
    HANDLE mutex_ = nullptr;
    Result result_ = Result::error;
    DWORD error_code_ = ERROR_SUCCESS;
};

}  // desktop_orb::platform 命名空间结束
