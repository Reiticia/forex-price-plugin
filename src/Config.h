#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fxprice
{
constexpr std::size_t kMaxSymbols = 8;

// 控制任务栏与工具提示显示哪些字段，以及小数位精度。四项开关注册默认全开。
struct DisplayOptions
{
    bool showBid{true};
    bool showAsk{true};
    bool showMid{true};
    bool showChange{true};
    // 小数位数：-1 表示自动（按行情价格推断）；0..10 表示固定显示位数。
    int precision{-1};

    bool none() const
    {
        return !(showBid || showAsk || showMid || showChange);
    }
};

struct Settings
{
    int refreshSeconds{10};
    std::vector<std::wstring> symbols{L"EURUSD"};
    DisplayOptions display;
};

std::vector<std::wstring> NormalizeSymbols(const std::vector<std::wstring>& values);

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
