// 文件职责：通过 Windows WinHTTP 调用兼容 OpenAI 聊天接口，并解析截图分析结果。
// 所属模块：platform/screenshot；只负责编码请求、网络通信和解码响应，不访问界面或截图句柄。
// 资源生命周期：HTTP 会话、连接与请求句柄在单次调用结束时由 RAII 自动关闭。
#include "desktop_orb/platform/screenshot/screenshot_ai_client.hpp"

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace desktop_orb::platform {
namespace {

constexpr DWORD kRequestTimeoutMilliseconds = 60000;
constexpr DWORD kMaximumResponseBytes = 10 * 1024 * 1024;
constexpr wchar_t kDefaultBaseUrl[] = L"https://api.openai.com/v1";
constexpr wchar_t kDefaultModel[] = L"gpt-4o-mini";

// 作用：在离开作用域时关闭一个 WinHTTP 句柄。
// 参数：handle 为 WinHTTP 创建的会话、连接或请求句柄。返回：无；空句柄不执行关闭。
struct WinHttpHandleDeleter {
    void operator()(HINTERNET handle) const noexcept {
        if (handle) WinHttpCloseHandle(handle);
    }
};

using WinHttpHandle = std::unique_ptr<void, WinHttpHandleDeleter>;

// 作用：把 UTF-8 文本转换为 Windows UTF-16 字符串。
// 参数：value 为有效 UTF-8 字节串。返回：转换后的宽字符串；非法或空输入返回空字符串。
std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value.data(), static_cast<int>(value.size()), result.data(), length)) {
        return {};
    }
    return result;
}

// 作用：把 UTF-16 字符串转换为 UTF-8 请求文本。
// 参数：value 为要发送的宽字符串。返回：UTF-8 字节串；空输入返回空字符串。
std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr)) {
        return {};
    }
    return result;
}

// 作用：将字符串转为 JSON 字符串内容，并转义引号、反斜杠和控制字符。
// 参数：value 为 UTF-8 文本。返回：不含外围双引号的 JSON 转义字节串。
std::string EscapeJson(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20) {
                escaped += "\\u00";
                escaped.push_back(hex[(character >> 4) & 0x0f]);
                escaped.push_back(hex[character & 0x0f]);
            } else {
                escaped.push_back(static_cast<char>(character));
            }
        }
    }
    return escaped;
}

// 作用：使用 Windows CryptoAPI 将 PNG 字节编码为单行 Base64。
// 参数：bytes 为非空 PNG 字节。返回：Base64 文本；编码失败时返回空字符串。
std::string EncodeBase64(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > std::numeric_limits<DWORD>::max()) return {};
    DWORD required = 0;
    if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &required) || required == 0) {
        return {};
    }
    std::string encoded(required, '\0');
    if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &required)) {
        return {};
    }
    if (required > 0 && encoded[required - 1] == '\0') --required;
    encoded.resize(required);
    return encoded;
}

// 作用：从 JSON 字符串解析一个十六进制四位 Unicode 转义值。
// 参数：input 为 JSON 文本；position 为反斜杠后的首个十六进制字符位置；code_unit 接收结果。
// 返回：恰有四个合法十六进制字符时为 true，否则为 false。
bool ParseHexCodeUnit(std::wstring_view input, std::size_t position, wchar_t& code_unit) {
    if (position + 4 > input.size()) return false;
    unsigned value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const wchar_t character = input[position + index];
        value <<= 4;
        if (character >= L'0' && character <= L'9') value |= character - L'0';
        else if (character >= L'a' && character <= L'f') value |= character - L'a' + 10;
        else if (character >= L'A' && character <= L'F') value |= character - L'A' + 10;
        else return false;
    }
    code_unit = static_cast<wchar_t>(value);
    return true;
}

// 作用：将一个 Unicode 码点追加到 UTF-16 字符串，处理 Windows 的代理项表示。
// 参数：output 接收 UTF-16 文本；code_point 为 Unicode 标量值。返回：无。
void AppendCodePoint(std::wstring& output, unsigned code_point) {
    if (code_point <= 0xffff) {
        output.push_back(static_cast<wchar_t>(code_point));
        return;
    }
    code_point -= 0x10000;
    output.push_back(static_cast<wchar_t>(0xd800 + (code_point >> 10)));
    output.push_back(static_cast<wchar_t>(0xdc00 + (code_point & 0x3ff)));
}

// 作用：读取 JSON 字符串并解码 JSON 规定的反斜杠与 Unicode 转义。
// 参数：input 为完整 JSON 文本；position 指向开引号并在成功时推进到闭引号之后。
// 返回：解析出的字符串；语法错误、截断转义或未闭合字符串返回空值。
std::optional<std::wstring> ParseJsonString(std::wstring_view input, std::size_t& position) {
    if (position >= input.size() || input[position] != L'"') return std::nullopt;
    ++position;
    std::wstring output;
    while (position < input.size()) {
        const wchar_t character = input[position++];
        if (character == L'"') return output;
        if (character < 0x20) return std::nullopt;
        if (character != L'\\') {
            output.push_back(character);
            continue;
        }
        if (position >= input.size()) return std::nullopt;
        const wchar_t escaped = input[position++];
        switch (escaped) {
        case L'"': output.push_back(L'"'); break;
        case L'\\': output.push_back(L'\\'); break;
        case L'/': output.push_back(L'/'); break;
        case L'b': output.push_back(L'\b'); break;
        case L'f': output.push_back(L'\f'); break;
        case L'n': output.push_back(L'\n'); break;
        case L'r': output.push_back(L'\r'); break;
        case L't': output.push_back(L'\t'); break;
        case L'u': {
            wchar_t first = 0;
            if (!ParseHexCodeUnit(input, position, first)) return std::nullopt;
            position += 4;
            if (first >= 0xd800 && first <= 0xdbff) {
                if (position + 6 > input.size() || input[position] != L'\\' ||
                    input[position + 1] != L'u') return std::nullopt;
                wchar_t second = 0;
                if (!ParseHexCodeUnit(input, position + 2, second) ||
                    second < 0xdc00 || second > 0xdfff) return std::nullopt;
                position += 6;
                const unsigned code_point = 0x10000 +
                    ((static_cast<unsigned>(first) - 0xd800) << 10) +
                    (static_cast<unsigned>(second) - 0xdc00);
                AppendCodePoint(output, code_point);
            } else if (first >= 0xdc00 && first <= 0xdfff) {
                return std::nullopt;
            } else {
                output.push_back(first);
            }
            break;
        }
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

class JsonValue {
public:
    enum class Type { null_value, boolean, number, string, array, object };

    Type type = Type::null_value;
    std::wstring string_value;
    std::vector<JsonValue> array_value;
    std::unordered_map<std::wstring, JsonValue> object_value;
};

class JsonParser {
public:
    // 作用：保存待解析的 JSON 文本。
    // 参数：input 为解码后的 UTF-16 JSON 文本。返回：无。
    explicit JsonParser(std::wstring_view input) : input_(input) {}

    // 作用：解析完整 JSON 值并拒绝尾随的非空白内容。
    // 参数：无。返回：成功时返回根值；语法错误时返回空值。
    std::optional<JsonValue> Parse() {
        SkipWhitespace();
        auto value = ParseValue(0);
        SkipWhitespace();
        if (!value || position_ != input_.size()) return std::nullopt;
        return value;
    }

private:
    // 作用：跳过 JSON 允许的空格、制表符和换行符。
    // 参数：无。返回：无。
    void SkipWhitespace() {
        while (position_ < input_.size() &&
               (input_[position_] == L' ' || input_[position_] == L'\t' ||
                input_[position_] == L'\r' || input_[position_] == L'\n')) {
            ++position_;
        }
    }

    // 作用：解析一个 JSON 值，并限制嵌套层级以防异常响应耗尽栈空间。
    // 参数：depth 为当前嵌套层级。返回：有效 JSON 值；格式非法或超过层级上限时返回空值。
    std::optional<JsonValue> ParseValue(unsigned depth) {
        if (depth > 64) return std::nullopt;
        SkipWhitespace();
        if (position_ >= input_.size()) return std::nullopt;
        if (input_[position_] == L'"') {
            auto string = ParseJsonString(input_, position_);
            if (!string) return std::nullopt;
            JsonValue value;
            value.type = JsonValue::Type::string;
            value.string_value = std::move(*string);
            return value;
        }
        if (input_[position_] == L'{') return ParseObject(depth + 1);
        if (input_[position_] == L'[') return ParseArray(depth + 1);
        if (ConsumeLiteral(L"true") || ConsumeLiteral(L"false")) {
            JsonValue value;
            value.type = JsonValue::Type::boolean;
            return value;
        }
        if (ConsumeLiteral(L"null")) return JsonValue{};
        if (input_[position_] == L'-' || (input_[position_] >= L'0' && input_[position_] <= L'9'))
            return ParseNumber();
        return std::nullopt;
    }

    // 作用：尝试消费指定 JSON 字面量。
    // 参数：literal 为 true、false 或 null。返回：匹配并推进位置时为 true。
    bool ConsumeLiteral(std::wstring_view literal) {
        if (input_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    // 作用：解析 JSON 对象及其字符串键值。
    // 参数：depth 为对象内部值的嵌套层级。返回：完整对象；分隔符或键值格式错误时返回空值。
    std::optional<JsonValue> ParseObject(unsigned depth) {
        ++position_;
        JsonValue object;
        object.type = JsonValue::Type::object;
        SkipWhitespace();
        if (ConsumeCharacter(L'}')) return object;
        for (;;) {
            SkipWhitespace();
            auto key = ParseJsonString(input_, position_);
            if (!key) return std::nullopt;
            SkipWhitespace();
            if (!ConsumeCharacter(L':')) return std::nullopt;
            auto value = ParseValue(depth);
            if (!value) return std::nullopt;
            object.object_value.insert_or_assign(std::move(*key), std::move(*value));
            SkipWhitespace();
            if (ConsumeCharacter(L'}')) return object;
            if (!ConsumeCharacter(L',')) return std::nullopt;
        }
    }

    // 作用：解析 JSON 数组及其成员。
    // 参数：depth 为数组成员的嵌套层级。返回：完整数组；分隔符或成员格式错误时返回空值。
    std::optional<JsonValue> ParseArray(unsigned depth) {
        ++position_;
        JsonValue array;
        array.type = JsonValue::Type::array;
        SkipWhitespace();
        if (ConsumeCharacter(L']')) return array;
        for (;;) {
            auto value = ParseValue(depth);
            if (!value) return std::nullopt;
            array.array_value.push_back(std::move(*value));
            SkipWhitespace();
            if (ConsumeCharacter(L']')) return array;
            if (!ConsumeCharacter(L',')) return std::nullopt;
        }
    }

    // 作用：解析 JSON 数字的整数、小数及指数部分。
    // 参数：无。返回：数字类型标记；数字语法非法时返回空值。
    std::optional<JsonValue> ParseNumber() {
        const std::size_t start = position_;
        if (input_[position_] == L'-') ++position_;
        if (position_ >= input_.size()) return std::nullopt;
        if (input_[position_] == L'0') {
            ++position_;
        } else {
            if (input_[position_] < L'1' || input_[position_] > L'9') return std::nullopt;
            while (position_ < input_.size() && input_[position_] >= L'0' && input_[position_] <= L'9')
                ++position_;
        }
        if (position_ < input_.size() && input_[position_] == L'.') {
            ++position_;
            const auto fraction_start = position_;
            while (position_ < input_.size() && input_[position_] >= L'0' && input_[position_] <= L'9')
                ++position_;
            if (fraction_start == position_) return std::nullopt;
        }
        if (position_ < input_.size() && (input_[position_] == L'e' || input_[position_] == L'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == L'+' || input_[position_] == L'-'))
                ++position_;
            const auto exponent_start = position_;
            while (position_ < input_.size() && input_[position_] >= L'0' && input_[position_] <= L'9')
                ++position_;
            if (exponent_start == position_) return std::nullopt;
        }
        if (start == position_) return std::nullopt;
        JsonValue number;
        number.type = JsonValue::Type::number;
        return number;
    }

    // 作用：消费当前位置上的指定 JSON 标点。
    // 参数：character 为预期标点。返回：匹配并推进位置时为 true，否则为 false。
    bool ConsumeCharacter(wchar_t character) {
        if (position_ >= input_.size() || input_[position_] != character) return false;
        ++position_;
        return true;
    }

    std::wstring_view input_;
    std::size_t position_ = 0;
};

// 作用：在 JSON 对象中按键名取得成员。
// 参数：object 为待查对象；key 为区分大小写的属性名。返回：找到的成员指针；类型错误或不存在时为空。
const JsonValue* FindMember(const JsonValue& object, std::wstring_view key) {
    if (object.type != JsonValue::Type::object) return nullptr;
    const auto found = object.object_value.find(std::wstring(key));
    return found == object.object_value.end() ? nullptr : &found->second;
}

// 作用：提取 OpenAI 兼容响应中的文本内容，兼容字符串及文本内容块数组。
// 参数：root 为已解析的响应根对象。返回：助手输出；缺少 choices/message/content 时返回空字符串。
std::wstring ExtractResponseText(const JsonValue& root) {
    const JsonValue* choices = FindMember(root, L"choices");
    if (!choices || choices->type != JsonValue::Type::array || choices->array_value.empty()) return {};
    const JsonValue* message = FindMember(choices->array_value.front(), L"message");
    const JsonValue* content = message ? FindMember(*message, L"content") : nullptr;
    if (!content) return {};
    if (content->type == JsonValue::Type::string) return content->string_value;
    if (content->type != JsonValue::Type::array) return {};

    std::wstring text;
    for (const auto& part : content->array_value) {
        const JsonValue* type = FindMember(part, L"type");
        const JsonValue* value = FindMember(part, L"text");
        if (value && value->type == JsonValue::Type::string &&
            (!type || type->type != JsonValue::Type::string || type->string_value == L"text")) {
            text += value->string_value;
        }
    }
    return text;
}

// 作用：从 HTTP 错误响应中读取 API 提供的 error.message。
// 参数：body 为 UTF-8 响应字节。返回：服务端错误文本；JSON 不可解析或无错误字段时返回空字符串。
std::wstring ExtractApiError(std::string_view body) {
    const auto wide_body = Utf8ToWide(body);
    const auto parsed = JsonParser(wide_body).Parse();
    if (!parsed) return {};
    const JsonValue* error = FindMember(*parsed, L"error");
    const JsonValue* message = error ? FindMember(*error, L"message") : nullptr;
    const JsonValue* code = error ? FindMember(*error, L"code") : nullptr;
    const JsonValue* type = error ? FindMember(*error, L"type") : nullptr;
    std::wstring detail = message && message->type == JsonValue::Type::string
        ? message->string_value : std::wstring{};
    // 核心逻辑：服务商常把 quota/rate limit 的机器可读分类放在 code 或 type，而非 message。
    for (const JsonValue* field : {code, type}) {
        if (field && field->type == JsonValue::Type::string && !field->string_value.empty()) {
            if (!detail.empty()) detail += L" ";
            detail += field->string_value;
        }
    }
    return detail;
}

// 作用：把服务端响应中的结构化错误或 HTTP 状态转换为用户可见文本。
// 参数：status 为 HTTP 状态码；body 为响应 UTF-8 内容。返回：可显示的错误信息。
std::wstring FormatHttpError(DWORD status, std::string_view body) {
    const auto api_error = ExtractApiError(body);
    if (status == 429) {
        std::wstring lower_error = api_error;
        std::transform(lower_error.begin(), lower_error.end(), lower_error.begin(),
                       [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
        if (lower_error.find(L"insufficient_quota") != std::wstring::npos ||
            lower_error.find(L"quota") != std::wstring::npos ||
            lower_error.find(L"billing") != std::wstring::npos) {
            return L"大模型服务返回 HTTP 429：账户额度或计费余额不足。请检查服务商账户余额、套餐和项目额度。" +
                (api_error.empty() ? std::wstring{} : L"\n服务端信息：" + api_error);
        }
        return L"大模型服务返回 HTTP 429：请求频率或并发数超出服务商限制。请稍后重试，并检查服务商的限流规则。" +
            (api_error.empty() ? std::wstring{} : L"\n服务端信息：" + api_error);
    }
    if (!api_error.empty()) return api_error;
    return L"大模型服务返回 HTTP 状态码 " + std::to_wstring(status) + L"。";
}

// 作用：将 Base URL 拆分为 WinHTTP 主机、端口、协议和请求路径。
// 参数：base_url 为配置中的服务根地址；host、port、secure、request_path 接收解析结果。
// 返回：支持的 HTTP(S) 地址解析成功为 true；地址缺少主机或包含无效路径时为 false。
bool ParseEndpoint(const std::wstring& base_url, std::wstring& host, INTERNET_PORT& port,
                   bool& secure, std::wstring& request_path) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(base_url.c_str(), 0, 0, &components) ||
        !components.lpszHostName || components.dwHostNameLength == 0) return false;
    if (components.nScheme != INTERNET_SCHEME_HTTPS && components.nScheme != INTERNET_SCHEME_HTTP)
        return false;

    host.assign(components.lpszHostName, components.dwHostNameLength);
    port = components.nPort;
    secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    if (components.dwUrlPathLength > 0)
        request_path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        const std::wstring extra(components.lpszExtraInfo, components.dwExtraInfoLength);
        const auto query = extra.find(L'?');
        if (query != std::wstring::npos) request_path += extra.substr(query);
    }

    const auto query = request_path.find(L'?');
    std::wstring suffix;
    if (query != std::wstring::npos) {
        suffix = request_path.substr(query);
        request_path.resize(query);
    }
    while (!request_path.empty() && request_path.back() == L'/') request_path.pop_back();
    request_path += L"/chat/completions";
    request_path += suffix;
    return true;
}

// 作用：构造含截图 Base64 内容的 OpenAI 兼容视觉聊天请求。
// 参数：model 为模型名；task 为文字提取或总结任务；encoded_image 为 PNG 的 Base64 文本。
// 返回：UTF-8 JSON 请求体。
std::string BuildRequestBody(const std::wstring& model, ScreenshotAiTask task,
                             const std::string& encoded_image) {
    const char* instruction = task == ScreenshotAiTask::extract_text
        ? "请识别截图中清晰可见的全部文字，尽量保留原有段落、列表和换行。只输出识别结果；不确定的内容标注为 [无法辨认]，不要补写截图之外的信息。"
        : "请用中文概括这张截图的主要内容。优先说明截图展示的对象、关键信息和可见状态，简洁分点回答；不确定的内容明确说明，不要推测截图之外的信息。";
    const auto model_utf8 = WideToUtf8(model);
    if (model_utf8.empty()) return {};
    std::string body;
    body.reserve(encoded_image.size() + model_utf8.size() + 600);
    body += R"({"model":")";
    body += EscapeJson(model_utf8);
    body += R"(","messages":[{"role":"user","content":[{"type":"text","text":")";
    body += instruction;
    body += R"("},{"type":"image_url","image_url":{"url":"data:image/png;base64,)";
    body += encoded_image;
    body += R"("}}]}],"max_tokens":1800})";
    return body;
}

// 作用：通过 WinHTTP 发送 JSON POST 请求并收集有限大小的响应正文。
// 参数：host、port、secure、request_path 为解析后的端点；api_key 为请求密钥；body 为 UTF-8 JSON。
// 返回：包含 HTTP 状态和正文的结果；网络错误或响应超过上限时返回失败说明。
struct HttpResponse {
    DWORD status = 0;
    std::string body;
    std::wstring error;
};

// 作用：执行一次使用系统代理和 Windows TLS 校验的 HTTPS 请求。
// 参数：host 为服务主机；port 为服务端口；secure 指示 TLS；request_path 为接口路径；
// api_key 为 Bearer 密钥；body 为 UTF-8 请求体。返回：HTTP 状态及响应正文或网络错误。
HttpResponse SendRequest(const std::wstring& host, INTERNET_PORT port, bool secure,
                         const std::wstring& request_path, const std::wstring& api_key,
                         const std::string& body) {
    WinHttpHandle session(WinHttpOpen(L"DesktopOrb/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return {0, {}, L"无法创建 WinHTTP 会话。"};
    WinHttpSetTimeouts(session.get(), kRequestTimeoutMilliseconds, kRequestTimeoutMilliseconds,
                       kRequestTimeoutMilliseconds, kRequestTimeoutMilliseconds);

    WinHttpHandle connection(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!connection) return {0, {}, L"无法连接到配置的大模型服务地址。"};

    const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(connection.get(), L"POST", request_path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) return {0, {}, L"无法创建大模型 HTTP 请求。"};

    const std::wstring authorization = L"Authorization: Bearer " + api_key;
    if (!WinHttpAddRequestHeaders(request.get(), L"Content-Type: application/json\r\n",
            static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpAddRequestHeaders(request.get(), authorization.c_str(), static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        return {0, {}, L"无法设置大模型请求头。"};
    }

    if (body.size() > std::numeric_limits<DWORD>::max() ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()), 0)) {
        return {0, {}, L"发送大模型请求失败，Windows 错误码：" + std::to_wstring(GetLastError())};
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr))
        return {0, {}, L"接收大模型响应失败，Windows 错误码：" + std::to_wstring(GetLastError())};

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        return {0, {}, L"无法读取大模型服务的 HTTP 状态码。"};
    }

    std::string response_body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available))
            return {status, {}, L"读取大模型响应失败，Windows 错误码：" + std::to_wstring(GetLastError())};
        if (available == 0) break;
        if (available > kMaximumResponseBytes || response_body.size() > kMaximumResponseBytes - available)
            return {status, {}, L"大模型响应超过允许的大小。"};
        const std::size_t old_size = response_body.size();
        response_body.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), response_body.data() + old_size, available, &read))
            return {status, {}, L"读取大模型响应正文失败，Windows 错误码：" + std::to_wstring(GetLastError())};
        response_body.resize(old_size + read);
    }
    return {status, std::move(response_body), {}};
}

// 作用：校验 API 配置并通过 WinHTTP 完成一次截图分析。
// 参数：image_png 为 PNG 原始字节；task 为任务类型；config 为用户设置的模型连接信息。
// 返回：分析文本或面向用户的错误信息；任何校验、网络或响应解析失败均返回失败状态。
ScreenshotAiResult AnalyzeScreenshotImpl(const std::vector<std::uint8_t>& image_png,
    ScreenshotAiTask task, const ScreenshotAiConfig& config) {
    if (image_png.empty()) return {false, {}, L"截图数据为空，无法发送分析。"};
    if (config.api_key.empty())
        return {false, {}, L"尚未配置大模型 API Key，请编辑程序目录中的 settings.ini，填写 [ScreenshotAI] ApiKey。"};

    const std::wstring base_url = config.base_url.empty() ? kDefaultBaseUrl : config.base_url;
    const std::wstring model = config.model.empty() ? kDefaultModel : config.model;
    std::wstring host;
    std::wstring request_path;
    INTERNET_PORT port = 0;
    bool secure = false;
    if (!ParseEndpoint(base_url, host, port, secure, request_path))
        return {false, {}, L"ScreenshotAI.BaseUrl 无效；请填写以 http:// 或 https:// 开头的服务根地址。"};
    // 核心逻辑：仅允许本机调试服务使用明文 HTTP，远程地址必须经过 TLS 保护 API Key。
    if (!secure && host != L"localhost" && host != L"127.0.0.1" && host != L"::1")
        return {false, {}, L"远程大模型服务必须使用 HTTPS，避免 API Key 通过明文 HTTP 发送。"};
    if (config.api_key.find_first_of(L"\r\n") != std::wstring::npos)
        return {false, {}, L"ScreenshotAI.ApiKey 不能包含换行符。"};

    const auto encoded_image = EncodeBase64(image_png);
    if (encoded_image.empty()) return {false, {}, L"无法将截图编码为 Base64。"};
    const auto body = BuildRequestBody(model, task, encoded_image);
    if (body.empty()) return {false, {}, L"模型名称不是有效的 UTF-16 文本。"};

    auto response = SendRequest(host, port, secure, request_path, config.api_key, body);
    if (!response.error.empty()) return {false, {}, std::move(response.error)};
    if (response.status < 200 || response.status >= 300)
        return {false, {}, FormatHttpError(response.status, response.body)};

    const auto wide_body = Utf8ToWide(response.body);
    const auto parsed = JsonParser(wide_body).Parse();
    if (!parsed) return {false, {}, L"大模型返回了无法解析的 JSON 响应。"};
    auto text = ExtractResponseText(*parsed);
    if (text.empty()) return {false, {}, L"大模型没有返回分析内容。"};
    return {true, std::move(text), {}};
}

}  // 匿名命名空间结束

ScreenshotAiResult AnalyzeScreenshot(const std::vector<std::uint8_t>& image_png,
    ScreenshotAiTask task, const ScreenshotAiConfig& config) {
    try {
        return AnalyzeScreenshotImpl(image_png, task, config);
    } catch (const std::exception&) {
        return {false, {}, L"截图分析过程中发生内存或系统资源错误。"};
    }
}

}  // desktop_orb::platform 命名空间结束
