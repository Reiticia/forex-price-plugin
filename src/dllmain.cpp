#include "Module.h"
#include "FxPricePlugin.h"

HINSTANCE g_moduleInstance = nullptr;

namespace
{
bool g_instanceCreated = false;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_moduleInstance = module;
        DisableThreadLibraryCalls(module);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // 通知插件停止后台刷新。函数内静态单例的析构不会在宿主卸载时
        // 执行，这里显式置位停止标志；工作线程创建前取得普通模块引用，
        // 因此即使宿主先调用 FreeLibrary 也不会崩溃。
        if (g_instanceCreated) fxprice::FxPricePlugin::Instance().Shutdown();
        g_moduleInstance = nullptr;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    g_instanceCreated = true;
    return &fxprice::FxPricePlugin::Instance();
}
