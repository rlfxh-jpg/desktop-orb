#pragma once

// 文件职责：声明截图专用的大模型视觉分析客户端。
// 客户端使用 WinHTTP 直接调用 OpenAI 兼容接口；不持有截图或界面资源。
#include <cstdint>
#include <string>
#include <vector>

namespace desktop_orb::platform {

// 作用：选择截图视觉分析任务。
// 参数：无。返回：无。
enum class ScreenshotAiTask { extract_text, summarize };

// 作用：承载一次截图视觉分析的结果或错误。
// 参数：无。返回：无；succeeded 为 false 时应读取 error。
struct ScreenshotAiResult {
    bool succeeded = false;
    std::wstring text;
    std::wstring error;
};

// 作用：承载一次截图分析所需的服务端连接配置。
// 参数：无。返回：无；api_key 为空时调用方应提示用户编辑设置文件。
struct ScreenshotAiConfig {
    std::wstring api_key;
    std::wstring base_url = L"https://api.openai.com/v1";
    std::wstring model = L"gpt-4o-mini";
};

// 作用：通过 WinHTTP 调用 OpenAI 兼容的视觉聊天接口分析 PNG 截图。
// 参数：image_png 为完整 PNG 字节；task 为文字提取或截图总结；config 为密钥、接口地址和模型配置。
// 返回：分析文本或可显示的错误信息；调用会阻塞当前线程，应放在后台线程执行。
[[nodiscard]] ScreenshotAiResult AnalyzeScreenshot(
    const std::vector<std::uint8_t>& image_png, ScreenshotAiTask task,
    const ScreenshotAiConfig& config);

}  // desktop_orb::platform 命名空间结束
