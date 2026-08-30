#include "Quote.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace fxprice
{
namespace
{
std::wstring AsciiToWide(const std::string& value)
{
    std::wstring result;
    result.reserve(value.size());
    for (const char character : value)
        result.push_back(static_cast<wchar_t>(character));
    return result;
}
} // namespace

std::wstring NormalizeSymbol(std::wstring value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](wchar_t character) {
        return std::iswspace(character) != 0 || character == L'/' ||
               character == L'-' || character == L'_';
    }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towupper(character));
    });
    return value;
}

bool IsValidSymbol(const std::wstring& value)
{
    return value.size() == 6 &&
           std::all_of(value.begin(), value.end(), [](wchar_t character) {
               return character >= L'A' && character <= L'Z';
           });
}

std::wstring DisplaySymbol(const std::wstring& value)
{
    if (!IsValidSymbol(value)) return value;
    return value.substr(0, 3) + L"/" + value.substr(3);
}

std::string WideToAscii(const std::wstring& value)
{
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value)
        result.push_back(static_cast<char>(character));
    return result;
}

std::wstring FormatNumber(double value, int precision)
{
    if (!std::isfinite(value)) return L"--";
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(std::clamp(precision, 0, 10)) << value;
    return stream.str();
}

std::wstring FormatPercentage(const std::optional<double>& value)
{
    if (!value || !std::isfinite(*value)) return L"--";
    std::wostringstream stream;
    if (*value >= 0.0) stream << L'+';
    stream << std::fixed << std::setprecision(2) << *value << L'%';
    return stream.str();
}

std::wstring FormatQuoteValue(const Quote& quote)
{
    const std::wstring bid = FormatNumber(quote.bid, quote.precision);
    const std::wstring ask = FormatNumber(quote.ask, quote.precision);
    const std::wstring midpoint = FormatNumber(quote.midpoint, quote.precision);
    return L"买 " + bid + L" 卖 " + ask + L" 中 " + midpoint + L" " +
           FormatPercentage(quote.dailyChangePercent);
}

std::wstring FormatQuoteSummary(const Quote& quote)
{
    return FormatNumber(quote.midpoint, quote.precision) + L" " +
           FormatPercentage(quote.dailyChangePercent);
}

std::wstring FormatQuoteDetail(const Quote& quote)
{
    const std::wstring bid = FormatNumber(quote.bid, quote.precision);
    const std::wstring ask = FormatNumber(quote.ask, quote.precision);
    const std::wstring midpoint = FormatNumber(quote.midpoint, quote.precision);
    std::wstring result;
    if (!quote.description.empty())
        result += AsciiToWide(quote.description) + L" | ";
    result += L"买一 " + bid + L" | 卖一 " + ask + L" | 中间价 " + midpoint +
              L" | 日涨跌 " + FormatPercentage(quote.dailyChangePercent);
    return result;
}
} // namespace fxprice
