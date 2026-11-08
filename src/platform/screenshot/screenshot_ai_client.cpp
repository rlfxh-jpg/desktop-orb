// 文件职责：在后台调用嵌入式 Python 截图分析模块，并转换其结果供应用层显示。
// Python 解释器仅在本次调用所在的工作线程内创建和销毁；不访问 Win32 界面对象。
#include "desktop_orb/platform/screenshot/screenshot_ai_client.hpp"

#include "desktop_orb/platform/common/asset_path.hpp"
#include <pybind11/embed.h>
#include <windows.h>
#include <exception>
#include <mutex>

namespace desktop_orb::platform {
namespace py = pybind11;
namespace {

// 作用：在进程内初始化一次 Python，并释放初始化线程持有的全局解释器锁。
// 参数：无。返回：无；解释器保留到进程退出，避免第三方扩展在重复 finalize 后损坏状态。
void EnsurePythonRuntime() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        py::initialize_interpreter();
        // 核心逻辑：嵌入式解释器跨多次截图请求复用；调用线程须释放 GIL，供后台任务重新获取。
        PyEval_SaveThread();
    });
}

// 作用：把 Python 错误中的 UTF-8 文本转换为 Windows 宽字符串。
// 参数：value 为 UTF-8 错误内容。返回：转换后的 UTF-16 文本；输入为空时返回空字符串。
std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return L"大模型模块返回了无法识别的错误信息。";
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length);
    return result;
}

}  // 匿名命名空间结束

ScreenshotAiResult AnalyzeScreenshot(
    const std::vector<std::uint8_t>& image_png, ScreenshotAiTask task) {
    if (image_png.empty()) return {false, {}, L"截图数据为空，无法发送分析。"};

    try {
        EnsurePythonRuntime();
        py::gil_scoped_acquire gil;
        try {
            const auto worker_directory = AssetPath(L"python");
            if (worker_directory.empty()) return {false, {}, L"无法定位截图大模型脚本目录。"};
            py::module_::import("sys").attr("path").attr("insert")(0, worker_directory.wstring());
            const py::object worker = py::module_::import("llm_worker");
            const py::bytes image(reinterpret_cast<const char*>(image_png.data()), image_png.size());
            const char* action = task == ScreenshotAiTask::extract_text ? "extract_text" : "summarize";
            const std::wstring text = worker.attr("analyze_screenshot")(image, action).cast<std::wstring>();
            if (text.empty()) return {false, {}, L"大模型没有返回分析内容。"};
            return {true, text, {}};
        } catch (const py::error_already_set& error) {
            // 核心逻辑：必须在解释器仍存活时读取 Python 异常对象，避免析构后访问失效对象。
            try {
                return {false, {}, error.value().attr("__str__")().cast<std::wstring>()};
            } catch (const std::exception&) {
                return {false, {}, Utf8ToWide(error.what())};
            }
        }
    } catch (const std::exception& error) {
        return {false, {}, Utf8ToWide(error.what())};
    }
}

}  // desktop_orb::platform 命名空间结束
