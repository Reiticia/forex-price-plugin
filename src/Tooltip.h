#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fxprice
{
// TrafficMonitor 1.85.1 merges every plugin tooltip into an MFC control with
// a 1024 UTF-16 code-unit limit. Reserve space for the host and other plugins.
constexpr std::size_t kMaxTooltipLength = 384;

std::wstring BuildCompactTooltip(const std::vector<std::wstring>& lines, bool weekendPaused);
} // namespace fxprice
