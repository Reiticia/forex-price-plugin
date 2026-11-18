#pragma once

#include "Quote.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace fxprice
{
// 构造 biquote /api/latest 批量查询串（重复 symbols 参数，不含域名部分）。
std::string BuildLatestQuery(const std::vector<std::wstring>& symbols);
// 解析 biquote /api/latest 响应（以标的代码为键的 tick 对象字典）。
bool ParseLatestResponse(const std::string& json,
                         std::unordered_map<std::string, Quote>& quotes,
                         std::string& error);
// Bitget CFD: 仅接受所选模式的精确代码；日涨跌取 bidOpenPriceChange × 100。
bool ParseBitgetCfdResponse(const std::string& json, QuoteSource source,
                            std::unordered_map<std::string, Quote>& quotes,
                            std::string& error);
} // namespace fxprice
