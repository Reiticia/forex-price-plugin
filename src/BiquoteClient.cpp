#include "BiquoteClient.h"

#include "MarketData.h"

#include <Windows.h>
#include <winhttp.h>

#include <memory>
#include <mutex>
#include <string>

namespace fxprice
{
namespace
{
constexpr std::size_t kMaxResponseBytes = 1024 * 1024;

std::string WindowsError(const char* operation)
{
    return std::string(operation) + " 失败，Windows 错误码 " +
           std::to_string(GetLastError());
}
// WinHTTP asynchronous callbacks only copy completion state; no HWND/plugin access.
struct AsyncRequest
{
    HANDLE completed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE closed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HINTERNET handle{}, connection{}, session{};
    bool notifyRegistered{};
    std::mutex mutex;
    DWORD expected{}, failure{}, count{};

    ~AsyncRequest()
    {
        if (handle) WinHttpCloseHandle(handle); // abort outstanding I/O
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
        // Unlike an event signalled inside HANDLE_CLOSING, the session unload
        // event proves that the last callback has RETURNED from our DLL.
        if (notifyRegistered) WaitForSingleObject(closed, INFINITE);
        if (completed) CloseHandle(completed);
        if (closed) CloseHandle(closed);
    }
    void Begin(DWORD status)
    {
        std::lock_guard<std::mutex> lock(mutex);
        expected = status;
        failure = count = 0;
        ResetEvent(completed);
    }
    bool Wait(const std::shared_ptr<RequestControl>& control, std::string& error)
    {
        for (;;)
        {
            if (!control->Check(error)) return false;
            const DWORD result = WaitForSingleObject(completed, 25);
            if (result == WAIT_TIMEOUT) continue;
            if (result != WAIT_OBJECT_0) { error = "等待行情响应失败"; return false; }
            std::lock_guard<std::mutex> lock(mutex);
            if (failure) { error = "行情网络请求失败，Windows 错误码 " + std::to_string(failure); return false; }
            return control->Check(error);
        }
    }
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status,
                                  void* info, DWORD length)
    {
        auto* self = reinterpret_cast<AsyncRequest*>(context);
        if (!self) return;
        std::lock_guard<std::mutex> lock(self->mutex);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        {
            self->failure = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
            SetEvent(self->completed);
        }
        else if (status == self->expected)
        {
            if (status == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE)
                self->count = *static_cast<DWORD*>(info);
            else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
                self->count = length;
            SetEvent(self->completed);
        }
    }
};
} // namespace

bool WinHttpTransport::Get(const std::wstring& host, const std::wstring& path,
                           std::string& response, std::string& error)
{
    return GetControlled(host, path, response, error, std::make_shared<RequestControl>());
}

bool WinHttpTransport::GetControlled(const std::wstring& host, const std::wstring& path,
                                     std::string& response, std::string& error,
                                     const std::shared_ptr<RequestControl>& control)
{
    response.clear();
    if (!control->Check(error)) return false;
    AsyncRequest request;
    if (!request.completed || !request.closed)
    {
        error = WindowsError("CreateEvent");
        return false;
    }
    request.session = WinHttpOpen(L"TrafficMonitor FxPricePlugin/0.4.0",
                                       WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!request.session)
    {
        error = WindowsError("WinHttpOpen");
        return false;
    }
    if (!WinHttpSetOption(request.session, WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT,
                           &request.closed, sizeof(request.closed)))
    {
        error = WindowsError("设置 WinHTTP 卸载事件");
        return false;
    }
    request.notifyRegistered = true;
    if (!WinHttpSetTimeouts(request.session, 5000, 5000, 8000, 8000))
    {
        error = WindowsError("WinHttpSetTimeouts");
        return false;
    }

    request.connection = WinHttpConnect(request.session, host.c_str(), port_, 0);
    if (!request.connection)
    {
        error = WindowsError("WinHttpConnect");
        return false;
    }

    HINTERNET handle = WinHttpOpenRequest(request.connection, L"GET", path.c_str(),
                                         nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!handle)
    {
        error = WindowsError("WinHttpOpenRequest");
        return false;
    }

    request.handle = handle;
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&request);
    if (!WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE,
                           &context, sizeof(context)) ||
        WinHttpSetStatusCallback(handle, AsyncRequest::Callback,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS, 0)
            == WINHTTP_INVALID_STATUS_CALLBACK)
    {
        error = WindowsError("设置异步行情回调");
        return false;
    }
    constexpr wchar_t headers[] = L"Accept: application/json\r\n";
    request.Begin(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    if (!WinHttpSendRequest(handle, headers, static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, context))
    {
        error = WindowsError("行情网络请求");
        return false;
    }

    if (!request.Wait(control, error)) return false;
    request.Begin(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    if (!WinHttpReceiveResponse(handle, nullptr))
    {
        error = WindowsError("接收行情响应");
        return false;
    }
    if (!request.Wait(control, error)) return false;
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(handle,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                             WINHTTP_NO_HEADER_INDEX))
    {
        error = WindowsError("读取 HTTP 状态码");
        return false;
    }
    if (statusCode != 200)
    {
        error = "行情服务返回 HTTP " + std::to_string(statusCode);
        return false;
    }

    for (;;)
    {
        request.Begin(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE);
        if (!WinHttpQueryDataAvailable(handle, nullptr))
        {
            error = WindowsError("读取响应长度");
            return false;
        }
        if (!request.Wait(control, error)) return false;
        const DWORD available = request.count;
        if (available == 0) break;
        if (available > kMaxResponseBytes - response.size())
        {
            error = "行情响应超过 1 MiB 限制";
            return false;
        }
        const std::size_t offset = response.size();
        response.resize(offset + available);
        request.Begin(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        if (!WinHttpReadData(handle, response.data() + offset, available, nullptr))
        {
            error = WindowsError("读取行情响应");
            return false;
        }
        if (!request.Wait(control, error)) return false;
        response.resize(offset + request.count);
    }

    if (!control->Check(error)) return false;
    error.clear();
    return true;
}

bool BiquoteClient::Fetch(const std::vector<std::wstring>& symbols,
                          std::unordered_map<std::string, Quote>& quotes,
                          std::string& error)
{
    const auto control = control_ ? control_ : std::make_shared<RequestControl>();
    quotes.clear();
    if (!control->Check(error)) return false;
    const std::string path = BuildLatestQuery(symbols);
    if (path.find('?') == std::string::npos)
    {
        error = "没有有效的交易对可查询";
        return false;
    }
    std::string response;
    if (!transport_.GetControlled(L"biquote.io", std::wstring(path.begin(), path.end()),
                                  response, error, control))
        return false;
    return ParseLatestResponse(response, quotes, error);
}

bool BiquoteClient::ValidateSymbol(const std::wstring& symbol, std::string& error)
{
    const std::wstring normalized = NormalizeSymbol(symbol);
    if (!IsValidSymbol(normalized))
    {
        error = "交易对格式无效";
        return false;
    }
    std::unordered_map<std::string, Quote> quotes;
    if (!Fetch({normalized}, quotes, error)) return false;
    if (quotes.find(WideToAscii(normalized)) == quotes.end())
    {
        error = "该交易对不存在或没有有效的买卖报价";
        return false;
    }
    error.clear();
    return true;
}
bool MarketClient::Fetch(const std::vector<std::wstring>& symbols,
                          std::unordered_map<std::string, Quote>& quotes, std::string& error)
{
    const auto control = control_ ? control_ : std::make_shared<RequestControl>();
    quotes.clear();
    if (!control->Check(error)) return false;
    if (source_ == QuoteSource::Biquote)
        return BiquoteClient(transport_, control).Fetch(symbols, quotes, error);
    std::vector<std::wstring> valid;
    for (const auto& symbol : symbols)
    {
        const auto normalized = NormalizeSymbol(symbol);
        if (IsValidSymbol(normalized)) valid.push_back(normalized);
    }
    if (valid.empty())
    {
        error = "没有有效的交易对可查询";
        return false;
    }
    // Bitget 的无 symbol 响应不包含 .pro，Pro 必须逐标的精确查询。
    if (source_ == QuoteSource::BitgetPro && valid.size() > 1)
    {
        for (const auto& symbol : valid)
        {
            std::unordered_map<std::string, Quote> one;
            if (!MarketClient(transport_, source_, control).Fetch({symbol}, one, error))
            {
                quotes.clear();
                return false;
            }
            quotes.insert(one.begin(), one.end());
        }
        return true;
    }
    // ECN / .s 多标的一次拉取所有 CFD tickers（实测约 40 KiB）。
    std::wstring path = L"/api/v3/cfd/market/tickers";
    if (valid.size() == 1) path += L"?symbol=" + DisplaySymbol(valid[0], source_);
    std::string response;
    if (!transport_.GetControlled(L"api.bitget.com", path, response, error, control)) return false;
    std::unordered_map<std::string, Quote> all;
    if (!ParseBitgetCfdResponse(response, source_, all, error)) return false;
    for (const auto& symbol : valid)
    {
        const auto key = WideToAscii(symbol);
        const auto found = all.find(key);
        if (found != all.end()) quotes.emplace(key, found->second);
    }
    if (quotes.empty())
    {
        error = "Bitget CFD 未返回所请求标的的有效买卖报价";
        return false;
    }
    error.clear();
    return true;
}

bool MarketClient::ValidateSymbol(const std::wstring& symbol, std::string& error)
{
    std::unordered_map<std::string, Quote> quotes;
    if (!Fetch({symbol}, quotes, error)) return false;
    if (quotes.find(WideToAscii(NormalizeSymbol(symbol))) == quotes.end())
    {
        error = "该交易对不存在或没有有效的买卖报价";
        return false;
    }
    error.clear();
    return true;
}
} // namespace fxprice
