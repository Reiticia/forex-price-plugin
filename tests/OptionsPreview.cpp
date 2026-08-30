#include "PluginInterface.h"

#include <Windows.h>

int wmain(int argumentCount, wchar_t* arguments[])
{
    if (argumentCount != 2) return 2;
    HMODULE module = LoadLibraryW(arguments[1]);
    if (!module) return 1;
    using GetInstance = ITMPlugin* (*)();
    const auto getInstance = reinterpret_cast<GetInstance>(
        GetProcAddress(module, "TMPluginGetInstance"));
    ITMPlugin* plugin = getInstance ? getInstance() : nullptr;
    if (!plugin)
    {
        FreeLibrary(module);
        return 3;
    }
    plugin->ShowOptionsDialog(nullptr);
    FreeLibrary(module);
    return 0;
}
