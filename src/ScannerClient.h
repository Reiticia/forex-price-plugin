#pragma once

#include "Quote.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace fxprice
{
class IHttpTransport
{
public:
    virtual ~IHttpTransport() = default;
    virtual bool PostJson(const std::wstring& host, const std::wstring& path,
                          const std::string& body, std::string& response,
                          std::string& error) = 0;
};

class WinHttpTransport final : public IHttpTransport
{
public:
    bool PostJson(const std::wstring& host, const std::wstring& path,
                  const std::string& body, std::string& response,
                  std::string& error) override;
};

class ScannerClient
{
public:
    explicit ScannerClient(IHttpTransport& transport) : transport_(transport) {}

    bool Fetch(const std::vector<std::wstring>& symbols,
               std::unordered_map<std::string, Quote>& quotes,
               std::string& error);

private:
    IHttpTransport& transport_;
};
} // namespace fxprice
