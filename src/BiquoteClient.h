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
    virtual bool Get(const std::wstring& host, const std::wstring& path,
                     std::string& response, std::string& error) = 0;
};

class WinHttpTransport final : public IHttpTransport
{
public:
    bool Get(const std::wstring& host, const std::wstring& path,
             std::string& response, std::string& error) override;
};

class BiquoteClient
{
public:
    explicit BiquoteClient(IHttpTransport& transport) : transport_(transport) {}

    bool Fetch(const std::vector<std::wstring>& symbols,
               std::unordered_map<std::string, Quote>& quotes,
               std::string& error);

private:
    IHttpTransport& transport_;
};
} // namespace fxprice
