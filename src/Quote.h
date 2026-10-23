#pragma once

#include "Config.h"

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

// 任务栏独立项文本：包含勾选的价格及日涨跌幅字段，标签+值紧凑排布。
// precision 为该标的配置的小数位数（-1 表示自动，用 quote.precision 推断）。
std::wstring FormatQuoteValue(const Quote& quote, const DisplayOptions& options, int precision);
// 汇总项主价格（优先取中间价，其次买价、卖价），按勾选追加日涨跌幅。
std::wstring FormatQuoteSummary(const Quote& quote, const DisplayOptions& options, int precision);
// 工具提示详细文本：完整中文标签，竖线分隔，仅包含勾选字段。
std::wstring FormatQuoteDetail(const Quote& quote, const DisplayOptions& options, int precision);
// 按勾选字段生成带占位值的示例文本（用于设置界面预览）。
std::wstring FormatQuoteSample(const DisplayOptions& options, int precision);
} // namespace fxprice
