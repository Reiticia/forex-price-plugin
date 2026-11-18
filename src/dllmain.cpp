#include "Module.h"
#include "FxPricePlugin.h"

HINSTANCE g_moduleInstance = nullptr;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_moduleInstance = module;
        DisableThreadLibraryCalls(module);
    }
    // No locks, cancellation callbacks, singleton access or waits under loader
    // lock. Workers hold a normal module reference until their C++ cleanup ends.
    return TRUE;
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &fxprice::FxPricePlugin::Instance();
}
