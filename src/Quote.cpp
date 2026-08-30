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

struct ValueBuilder
{
    static void Append(std::wstring& result, bool& first, bool enabled,
                       const wchar_t* label, const std::wstring& value)
    {
        if (!enabled) return;
        if (!first) result += L' ';
        first = false;
        result += label;
        result += L':';
        result += value;
    }
};
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

std::wstring FormatQuoteValue(const Quote& quote, const DisplayOptions& options, int precision)
{
    const int effective = precision >= 0 ? precision : quote.precision;
    std::wstring result;
    bool first = true;
    ValueBuilder::Append(result, first, options.showBid, L"买",
                         FormatNumber(quote.bid, effective));
    ValueBuilder::Append(result, first, options.showAsk, L"卖",
                         FormatNumber(quote.ask, effective));
    ValueBuilder::Append(result, first, options.showMid, L"中",
                         FormatNumber(quote.midpoint, effective));
    ValueBuilder::Append(result, first, options.showChange, L"涨跌",
                         FormatPercentage(quote.dailyChangePercent));
    return result;
}

std::wstring FormatQuoteSummary(const Quote& quote, const DisplayOptions& options, int precision)
{
    const int effective = precision >= 0 ? precision : quote.precision;
    double primary = 0.0;
    bool hasPrimary = false;
    if (options.showMid)
    {
        primary = quote.midpoint;
        hasPrimary = true;
    }
    else if (options.showBid)
    {
        primary = quote.bid;
        hasPrimary = true;
    }
    else if (options.showAsk)
    {
        primary = quote.ask;
        hasPrimary = true;
    }
    std::wstring result;
    if (hasPrimary) result = FormatNumber(primary, effective);
    if (options.showChange)
    {
        if (!result.empty()) result += L' ';
        result += FormatPercentage(quote.dailyChangePercent);
    }
    return result;
}

std::wstring FormatQuoteDetail(const Quote& quote, const DisplayOptions& options, int precision)
{
    const int effective = precision >= 0 ? precision : quote.precision;
    std::wstring result;
    bool first = true;
    auto append = [&](bool enabled, const wchar_t* label, const std::wstring& value) {
        if (!enabled) return;
        if (!first) result += L" | ";
        first = false;
        result += label;
        result += L' ';
        result += value;
    };
    append(options.showBid, L"买一", FormatNumber(quote.bid, effective));
    append(options.showAsk, L"卖一", FormatNumber(quote.ask, effective));
    append(options.showMid, L"中间价", FormatNumber(quote.midpoint, effective));
    append(options.showChange, L"日涨跌", FormatPercentage(quote.dailyChangePercent));
    // 仅当至少显示一个字段时才添加描述前缀，避免产生"desc | "尾部悬空。
    if (!quote.description.empty() && !result.empty())
        result = AsciiToWide(quote.description) + L" | " + result;
    return result;
}

std::wstring FormatQuoteSample(const DisplayOptions& options, int precision)
{
    const int effective = precision >= 0 ? precision : 5;
    const std::wstring placeholder = FormatNumber(0.0, effective);
    const std::wstring changePlaceholder = L"+0.00%";
    std::wstring result;
    bool first = true;
    ValueBuilder::Append(result, first, options.showBid, L"买", placeholder);
    ValueBuilder::Append(result, first, options.showAsk, L"卖", placeholder);
    ValueBuilder::Append(result, first, options.showMid, L"中", placeholder);
    ValueBuilder::Append(result, first, options.showChange, L"涨跌", changePlaceholder);
    return result;
}
} // namespace fxprice
