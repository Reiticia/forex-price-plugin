#include "BiquoteClient.h"

#include "MarketData.h"

#include <Windows.h>
#include <winhttp.h>

#include <memory>
#include <string>

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

bool WinHttpTransport::Get(const std::wstring& host, const std::wstring& path,
                           std::string& response, std::string& error)
{
    response.clear();
    InternetHandle session(WinHttpOpen(L"TrafficMonitor FxPricePlugin/0.4.0",
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

    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(),
                                              nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    if (!request)
    {
        error = WindowsError("WinHttpOpenRequest");
        return false;
    }

    constexpr wchar_t headers[] = L"Accept: application/json\r\n";
    if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
    {
        error = WindowsError("行情网络请求");
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
    if (statusCode != 200)
    {
        error = "行情服务返回 HTTP " + std::to_string(statusCode);
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
        if (available > kMaxResponseBytes - response.size())
        {
            error = "行情响应超过 1 MiB 限制";
            return false;
        }
        const std::size_t offset = response.size();
        response.resize(offset + available);
        DWORD received = 0;
        if (!WinHttpReadData(request.get(), response.data() + offset, available, &received))
        {
            error = WindowsError("读取行情响应");
            return false;
        }
        response.resize(offset + received);
    }

    error.clear();
    return true;
}

bool BiquoteClient::Fetch(const std::vector<std::wstring>& symbols,
                          std::unordered_map<std::string, Quote>& quotes,
                          std::string& error)
{
    const std::string path = BuildLatestQuery(symbols);
    if (path.find('?') == std::string::npos)
    {
        error = "没有有效的交易对可查询";
        return false;
    }
    std::string response;
    if (!transport_.Get(L"biquote.io", std::wstring(path.begin(), path.end()),
                        response, error))
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
    quotes.clear();
    if (source_ == QuoteSource::Biquote)
        return BiquoteClient(transport_).Fetch(symbols, quotes, error);
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
            if (!Fetch({symbol}, one, error))
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
    if (!transport_.Get(L"api.bitget.com", path, response, error)) return false;
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
