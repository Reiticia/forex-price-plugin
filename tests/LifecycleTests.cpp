#include "PluginInterface.h"
#include <Windows.h>
#include <iostream>

// No price/network-success assertion: the module must unload safely even if
// DNS/TLS fails or the peer never responds. The request has a 20s total budget.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        HMODULE module = LoadLibraryW(argv[1]);
        if (!module) return 3;
        const auto get = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module, "TMPluginGetInstance"));
        if (!get) return 4;
        ITMPlugin* plugin = get();
        if (!plugin || !plugin->GetItem(1)) return 5;
        if (attempt == 11)
        {
            // Drop host reference with an in-flight worker. No host calls after
            // FreeLibrary. Worker reference must defer unload until cleanup.
            const auto startTest = reinterpret_cast<void(*)()>(GetProcAddress(module, "StartTestRequest"));
            if (startTest) startTest();
            else plugin->OnPluginCommand(0, nullptr, nullptr);
        }
        if (!FreeLibrary(module)) return 6;
        const auto start = GetTickCount64();
        for (;;)
        {
            HMODULE remaining{};
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, argv[1], &remaining)) break;
            if (GetTickCount64() - start > 25000)
            { std::cerr << "Module failed to unload after request deadline\n"; return 7; }
            Sleep(20);
        }
    }
    std::cout << "Repeated load/unload and in-flight unload passed\n";
    return 0;
}
