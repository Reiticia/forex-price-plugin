#include "Config.h"

#include "Quote.h"

#include <Windows.h>

#include <algorithm>
#include <cwchar>
#include <optional>
#include <vector>

namespace fxprice
{
namespace
{
std::wstring ReadString(const std::wstring& path, const wchar_t* section,
                        const wchar_t* key)
{
    std::vector<wchar_t> buffer(256, L'\0');
    GetPrivateProfileStringW(section, key, L"", buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

// 读取小数位配置：缺省返回空值，脏值（非整数）回退 -1（自动推断）。
std::optional<int> ReadPrecision(const std::wstring& path, const wchar_t* section,
                                 const wchar_t* key)
{
    const std::wstring text = ReadString(path, section, key);
    if (text.empty()) return std::nullopt;
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    if (end != text.c_str() && *end == L'\0')
        return std::clamp(static_cast<int>(value), -1, 10);
    return -1;
}

} // namespace

std::vector<SymbolConfig> NormalizeSymbols(const std::vector<SymbolConfig>& values)
{
    std::vector<SymbolConfig> symbols;
    for (const SymbolConfig& value : values)
    {
        SymbolConfig symbol;
        symbol.code = NormalizeSymbol(value.code);
        symbol.precision = std::clamp(value.precision, -1, 10);
        if (!IsValidSymbol(symbol.code) ||
            std::find_if(symbols.begin(), symbols.end(), [&](const SymbolConfig& existing) {
                return existing.code == symbol.code;
            }) != symbols.end())
            continue;
        symbols.push_back(std::move(symbol));
        if (symbols.size() == kMaxSymbols) break;
    }
    return symbols;
}

void ConfigStore::SetDirectory(const std::wstring& directory)
{
    path_ = directory;
    if (!path_.empty() && path_.back() != L'\\' && path_.back() != L'/')
        path_.push_back(L'\\');
    path_ += L"FxPrice.ini";
}

Settings ConfigStore::Load() const
{
    Settings settings;
    if (path_.empty() || GetFileAttributesW(path_.c_str()) == INVALID_FILE_ATTRIBUTES)
        return settings;

    settings.refreshSeconds = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"General", L"RefreshSeconds", 10,
                                               path_.c_str())),
        2, 300);
    settings.display.showBid =
        GetPrivateProfileIntW(L"General", L"ShowBid", 1, path_.c_str()) != 0;
    settings.display.showAsk =
        GetPrivateProfileIntW(L"General", L"ShowAsk", 1, path_.c_str()) != 0;
    settings.display.showMid =
        GetPrivateProfileIntW(L"General", L"ShowMid", 1, path_.c_str()) != 0;
    settings.display.showChange =
        GetPrivateProfileIntW(L"General", L"ShowChange", 1, path_.c_str()) != 0;
    const std::optional<int> legacyPrecision =
        ReadPrecision(path_, L"General", L"DisplayPrecision");
    const int count = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"General", L"Count", 1, path_.c_str())),
        0, static_cast<int>(kMaxSymbols));
    std::vector<SymbolConfig> loaded;
    for (int index = 0; index < count; ++index)
    {
        const std::wstring key = L"Symbol" + std::to_wstring(index);
        SymbolConfig symbol;
        symbol.code = ReadString(path_, L"Symbols", key.c_str());
        const std::wstring precisionKey = L"Precision" + std::to_wstring(index);
        const std::optional<int> precision =
            ReadPrecision(path_, L"Symbols", precisionKey.c_str());
        symbol.precision = precision.value_or(legacyPrecision.value_or(-1));
        loaded.push_back(std::move(symbol));
    }
    loaded = NormalizeSymbols(loaded);
    if (!loaded.empty())
        settings.symbols = std::move(loaded);
    else if (legacyPrecision)
        settings.symbols[0].precision = *legacyPrecision;
    return settings;
}

bool ConfigStore::Save(const Settings& settings) const
{
    if (path_.empty()) return false;
    const auto write = [&](const wchar_t* section, const wchar_t* key,
                           const std::wstring& value) {
        return WritePrivateProfileStringW(section, key, value.c_str(), path_.c_str()) != FALSE;
    };

    std::vector<SymbolConfig> symbols = NormalizeSymbols(settings.symbols);
    if (symbols.empty()) symbols.push_back(SymbolConfig{});
    bool success = true;
    success &= write(L"General", L"RefreshSeconds",
                     std::to_wstring(std::clamp(settings.refreshSeconds, 2, 300)));
    success &= write(L"General", L"ShowBid", settings.display.showBid ? L"1" : L"0");
    success &= write(L"General", L"ShowAsk", settings.display.showAsk ? L"1" : L"0");
    success &= write(L"General", L"ShowMid", settings.display.showMid ? L"1" : L"0");
    success &= write(L"General", L"ShowChange",
                     settings.display.showChange ? L"1" : L"0");
    success &= write(L"General", L"Count", std::to_wstring(symbols.size()));
    for (std::size_t index = 0; index < symbols.size(); ++index)
    {
        const std::wstring key = L"Symbol" + std::to_wstring(index);
        const std::wstring precisionKey = L"Precision" + std::to_wstring(index);
        success &= write(L"Symbols", key.c_str(), symbols[index].code);
        success &= write(L"Symbols", precisionKey.c_str(),
                         std::to_wstring(std::clamp(symbols[index].precision, -1, 10)));
    }
    for (std::size_t index = symbols.size(); index < kMaxSymbols; ++index)
    {
        const std::wstring key = L"Symbol" + std::to_wstring(index);
        const std::wstring precisionKey = L"Precision" + std::to_wstring(index);
        success &= WritePrivateProfileStringW(L"Symbols", key.c_str(), nullptr,
                                              path_.c_str()) != FALSE;
        success &= WritePrivateProfileStringW(L"Symbols", precisionKey.c_str(), nullptr,
                                              path_.c_str()) != FALSE;
    }
    // 尽力刷新 WritePrivateProfileString 的写入缓存，降低系统崩溃时丢失配置
    // 的风险；该调用的返回值不可靠，不计入保存结果。
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path_.c_str());
    return success;
}
} // namespace fxprice
