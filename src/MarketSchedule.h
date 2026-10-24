#pragma once

namespace fxprice
{
// 输入 UTC 星期（0=周日，6=周六）和小时，均来自 GetSystemTime。
// 固定 UTC+3：UTC 21:00 起为下一天，不依赖电脑本地时区或夏令时。
constexpr bool IsWeekendUtc3(unsigned utcDayOfWeek, unsigned utcHour)
{
    const unsigned marketDay = (utcDayOfWeek + (utcHour >= 21 ? 1 : 0)) % 7;
    return marketDay == 0 || marketDay == 6;
}
} // namespace fxprice
