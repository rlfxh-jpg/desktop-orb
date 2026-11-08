// 文件职责：验证原生 C++ 截图分析客户端的校验、HTTP 请求和 JSON 响应解析。
// 测试通过本机回环 HTTP 服务模拟 OpenAI 兼容接口，不依赖外网或真实 API Key。
#include "desktop_orb/platform/screenshot/screenshot_ai_client.hpp"
#include "desktop_orb/platform/common/settings.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

// 作用：验证统一设置文件跟随运行程序落在 exe 同级目录，并可读写截图 AI 参数。
// 参数：无。返回：路径和 INI 往返数据均符合约定时为 true。
bool VerifySettingsStoreUsesExecutableDirectory() {
    wchar_t executable_path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable_path,
                                             static_cast<DWORD>(std::size(executable_path)));
    if (length == 0 || length >= std::size(executable_path)) return false;

    const std::filesystem::path executable_directory =
        std::filesystem::path(executable_path).parent_path().lexically_normal();
    const desktop_orb::platform::SettingsStore store;
    if (store.path().filename() != L"settings.ini" ||
        store.path().parent_path().lexically_normal() != executable_directory) {
        return false;
    }

    desktop_orb::platform::Settings settings;
    settings.hotkey = L"Ctrl+Shift+F8";
    settings.screenshot_ai_api_key = L"test-config-key";
    settings.screenshot_ai_base_url = L"https://example.invalid/v1";
    settings.screenshot_ai_model = L"test-vision-model";
    if (!store.Save(settings)) return false;

    const auto loaded = store.Load();
    return loaded.hotkey == settings.hotkey &&
        loaded.screenshot_ai_api_key == settings.screenshot_ai_api_key &&
        loaded.screenshot_ai_base_url == settings.screenshot_ai_base_url &&
        loaded.screenshot_ai_model == settings.screenshot_ai_model;
}

// 作用：在测试服务器接收缓冲区中查找 HTTP 请求头结束位置。
// 参数：request 为已读取的请求字节。返回：头部结束后的偏移；尚未收全时返回 npos。
std::size_t HeaderEnd(const std::string& request) {
    return request.find("\r\n\r\n");
}

// 作用：从本机 TCP 请求头中读取 Content-Length。
// 参数：headers 为完整 HTTP 请求头。返回：声明的正文长度；字段缺失或无效时返回 0。
std::size_t ContentLength(const std::string& headers) {
    const auto marker = headers.find("Content-Length:");
    if (marker == std::string::npos) return 0;
    const auto value_start = headers.find_first_not_of(" \t", marker + 15);
    if (value_start == std::string::npos) return 0;
    try {
        return static_cast<std::size_t>(std::stoull(headers.substr(value_start)));
    } catch (const std::exception&) {
        return 0;
    }
}

// 作用：接收完整 HTTP 请求并返回指定状态码和 JSON 响应。
// 参数：listener 为已监听的本机 TCP 套接字；valid_request 接收请求内容检查结果；
// status 为模拟状态码；response_body 为服务端 UTF-8 JSON 正文；expected_prompt 为请求提示词片段。
// 返回：无；客户端套接字在函数结束前关闭，接受或收发失败时 valid_request 保持 false。
void ServeOneRequest(SOCKET listener, bool& valid_request, unsigned status,
                     const std::string& response_body, const std::string& expected_prompt) {
    SOCKET client = accept(listener, nullptr, nullptr);
    if (client == INVALID_SOCKET) return;

    std::string request;
    char buffer[4096]{};
    std::size_t expected_size = 0;
    for (;;) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        request.append(buffer, static_cast<std::size_t>(received));
        const auto header_end = HeaderEnd(request);
        if (header_end != std::string::npos) {
            const auto header_size = header_end + 4;
            expected_size = header_size + ContentLength(request.substr(0, header_end));
            if (request.size() >= expected_size) break;
        }
    }

    const auto header_end = HeaderEnd(request);
    if (header_end != std::string::npos) {
        const std::string headers = request.substr(0, header_end);
        valid_request = headers.find("POST /v1/chat/completions HTTP/1.1") != std::string::npos &&
            headers.find("Authorization: Bearer test-api-key") != std::string::npos &&
            request.find("\"model\":\"test-model\"") != std::string::npos &&
            request.find("data:image/png;base64,iVBORw==") != std::string::npos &&
            (expected_prompt.empty() || request.find(expected_prompt) != std::string::npos);
        const std::string response = "HTTP/1.1 " + std::to_string(status) + " Test\r\nContent-Type: application/json\r\n" +
            "Content-Length: " + std::to_string(response_body.size()) +
            "\r\nConnection: close\r\n\r\n" + response_body;
        std::size_t sent_total = 0;
        while (sent_total < response.size()) {
            const int sent = send(client, response.data() + sent_total,
                static_cast<int>(response.size() - sent_total), 0);
            if (sent <= 0) break;
            sent_total += static_cast<std::size_t>(sent);
        }
    }
    shutdown(client, SD_BOTH);
    closesocket(client);
}

// 作用：通过本机 HTTP 服务验证任务提示词、成功响应解析及限流错误提示。
// 参数：status 为模拟状态码；response_body 为服务端 JSON；expected_text 为预期结果或错误片段；
// task 为待验证的分析任务；expected_prompt 为请求中应包含的任务提示词片段。
// 返回：请求格式有效且客户端结果符合预期时为 true。
bool VerifyLocalHttpRequest(unsigned status, const std::string& response_body,
                            const std::wstring& expected_text,
                            desktop_orb::platform::ScreenshotAiTask task,
                            const std::string& expected_prompt) {
    WSADATA winsock_data{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) return false;

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    bool ready = bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
        listen(listener, 1) == 0;
    int address_size = sizeof(address);
    ready = ready && getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_size) == 0;
    if (!ready) {
        closesocket(listener);
        WSACleanup();
        return false;
    }

    bool valid_request = false;
    std::thread server([listener, &valid_request, status, response_body, expected_prompt] {
        ServeOneRequest(listener, valid_request, status, response_body, expected_prompt);
    });
    const auto port = std::to_wstring(ntohs(address.sin_port));
    const desktop_orb::platform::ScreenshotAiConfig config{
        L"test-api-key", std::wstring(L"http://127.0.0.1:") + port + L"/v1", L"test-model"};
    const std::vector<std::uint8_t> image{0x89, 0x50, 0x4e, 0x47};
    const auto result = desktop_orb::platform::AnalyzeScreenshot(
        image, task, config);
    server.join();
    closesocket(listener);
    WSACleanup();
    if (status >= 200 && status < 300)
        return valid_request && result.succeeded && result.text == expected_text;
    return valid_request && !result.succeeded && result.error.find(expected_text) != std::wstring::npos;
}

}  // 匿名命名空间结束

// 作用：检查本地校验路径和完整回环 HTTP 分析流程。
// 参数：无。返回：所有校验和服务端往返测试通过时为 0，否则为 1。
int main() {
    using desktop_orb::platform::AnalyzeScreenshot;
    using desktop_orb::platform::ScreenshotAiConfig;
    using desktop_orb::platform::ScreenshotAiTask;

    if (!VerifySettingsStoreUsesExecutableDirectory()) {
        std::wcerr << L"统一设置文件未正确保存到测试程序同级目录。\n";
        return 1;
    }

    const std::vector<std::uint8_t> placeholder_png{0x89, 0x50, 0x4e, 0x47};
    const ScreenshotAiConfig empty_config{};

    const auto empty_image = AnalyzeScreenshot({}, ScreenshotAiTask::extract_text, empty_config);
    if (empty_image.succeeded || empty_image.error != L"截图数据为空，无法发送分析。") {
        std::wcerr << L"空截图校验失败：" << empty_image.error << L"\n";
        return 1;
    }

    const auto missing_key = AnalyzeScreenshot(
        placeholder_png, ScreenshotAiTask::summarize, empty_config);
    if (missing_key.succeeded || missing_key.error.find(L"ApiKey") == std::wstring::npos) {
        std::wcerr << L"API Key 校验失败：" << missing_key.error << L"\n";
        return 1;
    }

    const ScreenshotAiConfig invalid_endpoint{L"test-key", L"file:///invalid", L"test-model"};
    const auto invalid_url = AnalyzeScreenshot(
        placeholder_png, ScreenshotAiTask::extract_text, invalid_endpoint);
    if (invalid_url.succeeded || invalid_url.error.find(L"BaseUrl") == std::wstring::npos) {
        std::wcerr << L"BaseUrl 校验失败：" << invalid_url.error << L"\n";
        return 1;
    }

    if (!VerifyLocalHttpRequest(200,
            R"({"choices":[{"message":{"content":"第一行\n第二行 \u4e2d"}}]})", L"第一行\n第二行 中",
            ScreenshotAiTask::extract_text, "请识别截图中清晰可见的全部文字")) {
        std::wcerr << L"本机 HTTP 请求或 JSON 响应解析测试失败。\n";
        return 1;
    }
    if (!VerifyLocalHttpRequest(200,
            R"({"choices":[{"message":{"content":"这是一个软件设置页面，显示网络请求超限。"}}]})",
            L"这是一个软件设置页面，显示网络请求超限。",
            ScreenshotAiTask::summarize, "不要逐字抄录截图中的全部文字")) {
        std::wcerr << L"截图总结任务未发送禁止逐字转录的提示词。\n";
        return 1;
    }
    if (!VerifyLocalHttpRequest(429,
            R"({"error":{"message":"You exceeded your current quota","type":"insufficient_quota","code":"insufficient_quota"}})",
            L"账户额度或计费余额不足", ScreenshotAiTask::extract_text, {})) {
        std::wcerr << L"HTTP 429 额度错误未正确提示。\n";
        return 1;
    }
    if (!VerifyLocalHttpRequest(429,
            R"({"error":{"message":"Too many requests","type":"rate_limit_error","code":"rate_limit_exceeded"}})",
            L"请求频率或并发数超出服务商限制", ScreenshotAiTask::extract_text, {})) {
        std::wcerr << L"HTTP 429 频率限制未正确提示。\n";
        return 1;
    }
    return 0;
}
