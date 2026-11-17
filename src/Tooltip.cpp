#include "Tooltip.h"

namespace fxprice
{
std::wstring BuildCompactTooltip(const std::vector<std::wstring>& lines, bool weekendPaused)
{
    std::wstring result = L"biquote.io 外汇行情（◀买一 ▶卖一 ≈中间价 Δ日涨跌，非24h）";
    if (weekendPaused) result += L"\n周末休市，已暂停抓取（UTC+3）；周一 00:00 恢复。";
    constexpr wchar_t marker[] = L"\n…（更多见插件设置）";
    constexpr std::size_t contentLimit = kMaxTooltipLength - (sizeof(marker) / sizeof(wchar_t) - 1);
    for (const std::wstring& line : lines)
    {
        if (line.size() + 1 > contentLimit - result.size())
        {
            result += marker;
            break;
        }
        result += L'\n';
        result += line;
    }
    return result;
}
} // namespace fxprice
