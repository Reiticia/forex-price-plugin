#include "PluginInterface.h"

#include <Windows.h>

#include <iostream>
#include <string>

int wmain(int argumentCount, wchar_t* arguments[])
{
    if (argumentCount != 2) return 2;
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

    std::wstring value;
    bool complete = false;
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        plugin->DataRequired();
        value = plugin->GetItem(1)->GetItemValueText();
        if (value.find(L"买 ") != std::wstring::npos &&
            value.find(L"卖 ") != std::wstring::npos &&
            value.find(L"中 ") != std::wstring::npos)
        {
            complete = true;
            break;
        }
        Sleep(500);
    }
    Sleep(100);
    const std::wstring tooltip = plugin->GetTooltipInfo();
    const bool valid = complete && tooltip.find(L"日涨跌") != std::wstring::npos;
    std::wcout << L"value=" << value << L'\n' << tooltip << L'\n';
    FreeLibrary(module);
    return valid ? 0 : 4;
}
