#pragma once

#include "Quote.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace fxprice
{
std::string BuildScannerRequest(const std::vector<std::wstring>& symbols);
bool ParseScannerResponse(const std::string& json,
                          std::unordered_map<std::string, Quote>& quotes,
                          std::string& error);
} // namespace fxprice
