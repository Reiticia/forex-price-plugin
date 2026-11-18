#pragma once

#include "Quote.h"
#include "RequestControl.h"

#include <memory>

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace fxprice
{
class IHttpTransport
{
public:
    virtual ~IHttpTransport() = default;
    virtual bool Get(const std::wstring& host, const std::wstring& path,
                     std::string& response, std::string& error) = 0;
    // Compatibility for test/custom transports; production transport aborts in-flight I/O.
    virtual bool GetControlled(const std::wstring& host, const std::wstring& path,
                               std::string& response, std::string& error,
                               const std::shared_ptr<RequestControl>& control)
    {
        if (!control->Check(error)) return false;
        return Get(host, path, response, error) && control->Check(error);
    }
};

class WinHttpTransport final : public IHttpTransport
{
public:
    explicit WinHttpTransport(unsigned short port = 443) : port_(port) {}
    bool Get(const std::wstring& host, const std::wstring& path,
             std::string& response, std::string& error) override;
    bool GetControlled(const std::wstring& host, const std::wstring& path,
                       std::string& response, std::string& error,
                       const std::shared_ptr<RequestControl>& control) override;
private:
    unsigned short port_;
};

class BiquoteClient
{
public:
    explicit BiquoteClient(IHttpTransport& transport,
        std::shared_ptr<RequestControl> control = nullptr)
        : transport_(transport), control_(std::move(control)) {}

    bool Fetch(const std::vector<std::wstring>& symbols,
               std::unordered_map<std::string, Quote>& quotes,
               std::string& error);

    // 仅在目标标的返回有效买卖报价时通过；不能用其他标的的报价代替。
    bool ValidateSymbol(const std::wstring& symbol, std::string& error);

private:
    IHttpTransport& transport_;
    std::shared_ptr<RequestControl> control_;
};
// 统一路由，禁止静默跨数据源或 CFD 模式回退。
class MarketClient
{
public:
    MarketClient(IHttpTransport& transport, QuoteSource source,
                 std::shared_ptr<RequestControl> control = nullptr)
        : transport_(transport), source_(source), control_(std::move(control)) {}
    bool Fetch(const std::vector<std::wstring>& symbols,
               std::unordered_map<std::string, Quote>& quotes, std::string& error);
    bool ValidateSymbol(const std::wstring& symbol, std::string& error);
private:
    IHttpTransport& transport_;
    QuoteSource source_;
    std::shared_ptr<RequestControl> control_;
};
} // namespace fxprice
