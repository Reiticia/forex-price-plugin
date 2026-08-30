#include "ScannerClient.h"

#include "MarketData.h"

#include <Windows.h>
#include <winhttp.h>

#include <memory>

namespace fxprice
{
namespace
{
constexpr std::size_t kMaxResponseBytes = 1024 * 1024;

struct InternetHandleCloser
{
    void operator()(void* handle) const
    {
        if (handle) WinHttpCloseHandle(handle);
    }
};
using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

std::string WindowsError(const char* operation)
{
    return std::string(operation) + " 失败，Windows 错误码 " +
           std::to_string(GetLastError());
}
} // namespace

bool WinHttpTransport::PostJson(const std::wstring& host, const std::wstring& path,
                                const std::string& body, std::string& response,
                                std::string& error)
{
    response.clear();
    InternetHandle session(WinHttpOpen(L"TrafficMonitor FxPricePlugin/1.0",
                                       WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
    {
        error = WindowsError("WinHttpOpen");
        return false;
    }
    if (!WinHttpSetTimeouts(session.get(), 5000, 5000, 8000, 8000))
    {
        error = WindowsError("WinHttpSetTimeouts");
        return false;
    }

    InternetHandle connection(WinHttpConnect(session.get(), host.c_str(),
                                              INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection)
    {
        error = WindowsError("WinHttpConnect");
        return false;
    }

    InternetHandle request(WinHttpOpenRequest(connection.get(), L"POST", path.c_str(),
                                              nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    if (!request)
    {
        error = WindowsError("WinHttpOpenRequest");
        return false;
    }

    constexpr wchar_t headers[] =
        L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
    if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L),
                            body.empty() ? WINHTTP_NO_REQUEST_DATA
                                         : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
    {
        error = WindowsError("TradingView 网络请求");
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(),
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                             WINHTTP_NO_HEADER_INDEX))
    {
        error = WindowsError("读取 HTTP 状态码");
        return false;
    }

    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available))
        {
            error = WindowsError("读取响应长度");
            return false;
        }
        if (available == 0) break;
        if (response.size() + available > kMaxResponseBytes)
        {
            error = "TradingView 响应超过 1 MiB 限制";
            return false;
        }
        const std::size_t offset = response.size();
        response.resize(offset + available);
        DWORD received = 0;
        if (!WinHttpReadData(request.get(), response.data() + offset, available, &received))
        {
            error = WindowsError("读取 TradingView 响应");
            return false;
        }
        response.resize(offset + received);
    }

    if (statusCode != 200)
    {
        error = "TradingView 返回 HTTP " + std::to_string(statusCode);
        return false;
    }
    error.clear();
    return true;
}

bool ScannerClient::Fetch(const std::vector<std::wstring>& symbols,
                          std::unordered_map<std::string, Quote>& quotes,
                          std::string& error)
{
    const std::string request = BuildScannerRequest(symbols);
    std::string response;
    if (!transport_.PostJson(L"scanner.tradingview.com", L"/forex/scan",
                             request, response, error))
        return false;
    return ParseScannerResponse(response, quotes, error);
}
} // namespace fxprice
