#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fxprice
{
constexpr std::size_t kMaxSymbols = 8;

// 持久化为稳定字符串，不依赖枚举序号。Bybit MT5 CFD 不属于 V5 API。
enum class QuoteSource { Biquote, BitgetEcn, BitgetStandard, BitgetPro };

inline const wchar_t* SourceId(QuoteSource source)
{
    switch (source)
    {
    case QuoteSource::BitgetEcn: return L"bitget-ecn";
    case QuoteSource::BitgetStandard: return L"bitget-standard";
    case QuoteSource::BitgetPro: return L"bitget-pro";
    default: return L"biquote";
    }
}

inline const wchar_t* SourceName(QuoteSource source)
{
    switch (source)
    {
    case QuoteSource::BitgetEcn: return L"Bitget CFD ECN";
    case QuoteSource::BitgetStandard: return L"Bitget CFD .s";
    case QuoteSource::BitgetPro: return L"Bitget CFD Pro";
    default: return L"biquote.io";
    }
}

inline const wchar_t* SourceSuffix(QuoteSource source)
{
    switch (source)
    {
    case QuoteSource::BitgetEcn: return L"";
    case QuoteSource::BitgetPro: return L".pro";
    default: return L".s";
    }
}

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
    QuoteSource source{QuoteSource::Biquote};
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
