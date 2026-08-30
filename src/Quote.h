#pragma once

#include <optional>
#include <string>

namespace fxprice
{
struct Quote
{
    std::string symbol;
    std::string description;
    double bid{};
    double ask{};
    double midpoint{};
    std::optional<double> dailyChangePercent;
    int precision{5};
};

std::wstring NormalizeSymbol(std::wstring value);
bool IsValidSymbol(const std::wstring& value);
std::wstring DisplaySymbol(const std::wstring& value);
std::string WideToAscii(const std::wstring& value);
std::wstring FormatNumber(double value, int precision);
std::wstring FormatPercentage(const std::optional<double>& value);
std::wstring FormatQuoteValue(const Quote& quote);
std::wstring FormatQuoteSummary(const Quote& quote);
std::wstring FormatQuoteDetail(const Quote& quote);
} // namespace fxprice
