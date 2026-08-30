#include "PluginInterface.h"

#include <Windows.h>

#include <iostream>

int wmain(int argumentCount, wchar_t* arguments[])
{
    if (argumentCount != 2)
    {
        std::wcerr << L"Usage: FxPricePluginSmoke <dll>\n";
        return 2;
    }
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
    bool valid = plugin && plugin->GetAPIVersion() == 8 &&
                 plugin->GetInfo(ITMPlugin::TMI_NAME) &&
                 *plugin->GetInfo(ITMPlugin::TMI_NAME) &&
                 plugin->GetItem(0) && plugin->GetItem(1);
    if (!FreeLibrary(module)) valid = false;
    if (!valid)
    {
        std::wcerr << L"Plugin ABI smoke test failed\n";
        return 3;
    }
    std::wcout << L"Plugin ABI smoke test passed\n";
    return 0;
}
