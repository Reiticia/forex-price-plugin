#include "Config.h"

#include "Quote.h"

#include <Windows.h>

#include <algorithm>
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

} // namespace

std::vector<std::wstring> NormalizeSymbols(const std::vector<std::wstring>& values)
{
    std::vector<std::wstring> symbols;
    for (const std::wstring& value : values)
    {
        const std::wstring symbol = NormalizeSymbol(value);
        if (!IsValidSymbol(symbol) ||
            std::find(symbols.begin(), symbols.end(), symbol) != symbols.end())
            continue;
        symbols.push_back(symbol);
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
    const int count = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"General", L"Count", 1, path_.c_str())),
        0, static_cast<int>(kMaxSymbols));
    std::vector<std::wstring> loaded;
    for (int index = 0; index < count; ++index)
    {
        const std::wstring key = L"Symbol" + std::to_wstring(index);
        loaded.push_back(ReadString(path_, L"Symbols", key.c_str()));
    }
    loaded = NormalizeSymbols(loaded);
    if (!loaded.empty()) settings.symbols = std::move(loaded);
    return settings;
}

bool ConfigStore::Save(const Settings& settings) const
{
    if (path_.empty()) return false;
    const auto write = [&](const wchar_t* section, const wchar_t* key,
                           const std::wstring& value) {
        return WritePrivateProfileStringW(section, key, value.c_str(), path_.c_str()) != FALSE;
    };

    std::vector<std::wstring> symbols = NormalizeSymbols(settings.symbols);
    if (symbols.empty()) symbols.push_back(L"EURUSD");
    bool success = true;
    success &= write(L"General", L"RefreshSeconds",
                     std::to_wstring(std::clamp(settings.refreshSeconds, 2, 300)));
    success &= write(L"General", L"Count", std::to_wstring(symbols.size()));
    for (std::size_t index = 0; index < symbols.size(); ++index)
    {
        const std::wstring key = L"Symbol" + std::to_wstring(index);
        success &= write(L"Symbols", key.c_str(), symbols[index]);
    }
    for (std::size_t index = symbols.size(); index < kMaxSymbols; ++index)
    {
        const std::wstring key = L"Symbol" + std::to_wstring(index);
        success &= WritePrivateProfileStringW(L"Symbols", key.c_str(), nullptr,
                                              path_.c_str()) != FALSE;
    }
    // 尽力刷新 WritePrivateProfileString 的写入缓存，降低系统崩溃时丢失配置
    // 的风险；该调用的返回值不可靠，不计入保存结果。
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path_.c_str());
    return success;
}
} // namespace fxprice
