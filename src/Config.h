#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fxprice
{
constexpr std::size_t kMaxSymbols = 8;

struct Settings
{
    int refreshSeconds{10};
    std::vector<std::wstring> symbols{L"EURUSD"};
};

std::vector<std::wstring> NormalizeSymbols(const std::vector<std::wstring>& values);

class ConfigStore
{
public:
    void SetDirectory(const std::wstring& directory);
    Settings Load() const;
    bool Save(const Settings& settings) const;
    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
};
} // namespace fxprice
