#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fxprice
{
constexpr std::size_t kMaxSymbols = 8;

// 单个外汇标的：代码 + 该标的独立的价格小数位数（-1 自动推断，0..10 固定）。
struct SymbolConfig
{
    std::wstring code{L"EURUSD"};
    // 小数位数：-1 表示自动（按行情价格推断）；0..10 表示固定显示位数。
    int precision{-1};
};

// 全局显示字段开关（作用于所有标的），不包含小数位数（后者按标的独立）。
struct DisplayOptions
{
    bool showBid{true};
    bool showAsk{true};
    bool showMid{true};
    bool showChange{true};

    bool none() const
    {
        return !(showBid || showAsk || showMid || showChange);
    }
};

struct Settings
{
    int refreshSeconds{10};
    std::vector<SymbolConfig> symbols{{}};
    DisplayOptions display;
};

std::vector<SymbolConfig> NormalizeSymbols(const std::vector<SymbolConfig>& values);

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
