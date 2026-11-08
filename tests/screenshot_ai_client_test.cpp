// 文件职责：验证截图 Python 分析模块的异常能安全转换为 C++ 结果。
// 测试使用非空占位 PNG 和空密钥，不会发起网络请求或访问真实截图。
#include "desktop_orb/platform/screenshot/screenshot_ai_client.hpp"

#include <windows.h>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

// 作用：验证缺少 API 密钥时 Python 异常会作为失败结果返回。
// 参数：无。返回：符合预期时为 0；异常转换不正确时为 1。
int main() {
    SetEnvironmentVariableW(L"OPENAI_API_KEY", nullptr);
    const std::vector<std::uint8_t> placeholder_png{0x89, 0x50, 0x4e, 0x47};
    for (const auto task : {desktop_orb::platform::ScreenshotAiTask::extract_text,
                            desktop_orb::platform::ScreenshotAiTask::summarize}) {
        desktop_orb::platform::ScreenshotAiResult result;
        std::thread worker([&] {
            result = desktop_orb::platform::AnalyzeScreenshot(placeholder_png, task);
        });
        worker.join();
        if (result.succeeded || result.error != L"请先设置 OPENAI_API_KEY 环境变量。") {
            std::wcerr << L"Unexpected screenshot AI result: " << result.error << L"\n";
            return 1;
        }
    }
    return 0;
}
