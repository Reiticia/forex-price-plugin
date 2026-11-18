#include "BiquoteClient.h"
#include "FxPricePlugin.h"
#include "Module.h"

HINSTANCE g_moduleInstance{};
namespace
{
// Deterministic in-flight worker: no Internet and independent of market hours.
class DelayedTransport final : public fxprice::IHttpTransport
{
public:
    bool Get(const std::wstring&, const std::wstring&, std::string& body, std::string&) override
    {
        Sleep(300);
        body = R"({"EURUSD":{"bid":1.1,"ask":1.2}})";
        return true;
    }
};
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) g_moduleInstance = module;
    return TRUE;
}
namespace fxprice
{
struct PluginTestAccess
{
    static void Start(FxPricePlugin& plugin)
    {
        std::lock_guard<std::mutex> lock(plugin.stateMutex_);
        plugin.LaunchWorkerLocked(); // weekday-independent lifecycle test only
    }
};
}
ITMPlugin* GetFixturePlugin();
extern "C" __declspec(dllexport) void StartTestRequest()
{
    fxprice::PluginTestAccess::Start(*static_cast<fxprice::FxPricePlugin*>(GetFixturePlugin()));
}
ITMPlugin* GetFixturePlugin()
{
    static DelayedTransport transport;
    static fxprice::FxPricePlugin plugin(&transport);
    return &plugin;
}
extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() { return GetFixturePlugin(); }
