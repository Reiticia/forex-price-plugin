#include "PluginInterface.h"
#include "Tooltip.h"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string>

int wmain(int argumentCount, wchar_t* arguments[])
{
    if (argumentCount != 2 && argumentCount != 3) return 2;
    std::wstring source = argumentCount == 3 ? arguments[2] : L"biquote";
    if (source != L"biquote" && source != L"bitget-ecn" &&
        source != L"bitget-standard" && source != L"bitget-pro") return 2;
    struct TemporaryConfig
    {
        std::filesystem::path directory;
        ~TemporaryConfig() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } config{std::filesystem::temp_directory_path() /
        (L"FxPriceLive-" + std::to_wstring(GetCurrentProcessId()))};
    std::filesystem::create_directories(config.directory);
    const auto ini = config.directory / L"FxPrice.ini";
    WritePrivateProfileStringW(L"General", L"Source", source.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"General", L"Count", L"1", ini.c_str());
    WritePrivateProfileStringW(L"Symbols", L"Symbol0", L"EURUSD", ini.c_str());
    HMODULE module = LoadLibraryW(arguments[1]);
    if (!module)
    {
        std::wcerr << L"LoadLibrary failed: " << GetLastError() << L'\n';
        return 1;
    }
    using GetInstance = ITMPlugin* (*)();
    const auto getInstance = reinterpret_cast<GetInstance>(
        GetProcAddress(module, "TMPluginGetInstance"));
    ITMPlugin* plugin = getInstance ? getInstance() : nullptr;
    if (!plugin || !plugin->GetItem(1))
    {
        FreeLibrary(module);
        return 3;
    }

    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, config.directory.c_str());
    std::wstring value;
    bool complete = false;
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        plugin->DataRequired();
        value = plugin->GetItem(1)->GetItemValueText();
        if (value.find(L"◀") != std::wstring::npos &&
            value.find(L"▶") != std::wstring::npos &&
            value.find(L"≈") != std::wstring::npos &&
            value.find(L"Δ") != std::wstring::npos)
        {
            complete = true;
            break;
        }
        Sleep(500);
    }
    Sleep(100);
    const std::wstring tooltip = plugin->GetTooltipInfo();
    const bool sourceMatches = tooltip.find(source == L"biquote" ? L"biquote.io" : L"Bitget CFD") != std::wstring::npos;
    const bool valid = complete && sourceMatches && tooltip.find(L"日涨跌") != std::wstring::npos &&
        tooltip.find(value) != std::wstring::npos && tooltip.size() <= fxprice::kMaxTooltipLength;
    std::wcout << L"value=" << value << L'\n' << tooltip << L'\n';
    FreeLibrary(module);
    return valid ? 0 : 4;
}
