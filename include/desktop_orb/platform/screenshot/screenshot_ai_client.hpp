#pragma once

// 文件职责：声明截图专用的大模型视觉分析客户端。
// 客户端通过嵌入式 Python 调用 OpenAI 兼容 SDK；不持有截图或界面资源。
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

// 作用：调用 assets/python/llm_worker.py 分析 PNG 截图。
// 参数：image_png 为完整 PNG 字节；task 为文字提取或截图总结。
// 返回：分析文本或可显示的错误信息；调用会阻塞当前线程，应放在后台线程执行。
[[nodiscard]] ScreenshotAiResult AnalyzeScreenshot(
    const std::vector<std::uint8_t>& image_png, ScreenshotAiTask task);

}  // desktop_orb::platform 命名空间结束
