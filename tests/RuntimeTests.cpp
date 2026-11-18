#include <winsock2.h>
#include <ws2tcpip.h>
#include "BiquoteClient.h"
#include "FxPricePlugin.h"
#include "Module.h"

#include <atomic>
#include <iostream>
#include <thread>

HINSTANCE g_moduleInstance{};
namespace fxprice
{
struct PluginTestAccess
{
    static void Apply(FxPricePlugin& plugin, Settings settings) { plugin.ApplySettings(std::move(settings)); }
    static bool Running(FxPricePlugin& plugin)
    { std::lock_guard<std::mutex> lock(plugin.stateMutex_); return plugin.workerRunning_; }
    static void ApplyPending(FxPricePlugin& plugin) { plugin.ApplyPendingResult(); }
    static void Start(FxPricePlugin& plugin)
    { std::lock_guard<std::mutex> lock(plugin.stateMutex_); plugin.LaunchWorkerLocked(); }
    static void SeedStale(FxPricePlugin& plugin)
    {
        std::lock_guard<std::mutex> lock(plugin.stateMutex_);
        plugin.pending_.generation = plugin.generation_ - 1;
        plugin.pending_.success = true;
        Quote quote; quote.symbol = "EURUSD"; quote.bid = 999; quote.ask = 1000; quote.midpoint = 999.5;
        plugin.pending_.quotes[quote.symbol] = quote;
        plugin.hasPending_ = true;
    }
};
}
namespace
{
int failures{};
void Check(bool value, const char* message)
{ if (!value) { std::cerr << "FAILED: " << message << '\n'; ++failures; } }

class SlowProTransport final : public fxprice::IHttpTransport
{
public:
    int requests{};
    bool Get(const std::wstring&, const std::wstring& path, std::string& body, std::string&) override
    {
        ++requests;
        Sleep(80);
        const auto symbol = path.substr(path.find(L"symbol=") + 7);
        body = "{\"code\":\"00000\",\"data\":[{\"symbol\":\"" +
            fxprice::WideToAscii(symbol) + "\",\"bid1\":\"1.1\",\"ask1\":\"1.2\"}]}";
        return true;
    }
};

class BlockingTransport final : public fxprice::IHttpTransport
{
public:
    std::atomic<bool> entered{}, cancelled{};
    bool Get(const std::wstring&, const std::wstring&, std::string&, std::string&) override { return false; }
    bool GetControlled(const std::wstring&, const std::wstring&, std::string&, std::string& error,
                       const std::shared_ptr<fxprice::RequestControl>& control) override
    {
        entered = true;
        while (control->Check(error)) Sleep(5);
        cancelled = true;
        return false;
    }
};

// A TCP peer that accepts TLS bytes but never replies. No Internet, certificate,
// timing of a real exchange, or system clock changes required.
void TestBlockedHttp(bool cancel)
{
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind loopback");
    int size = sizeof(address);
    getsockname(server, reinterpret_cast<sockaddr*>(&address), &size);
    listen(server, 1);
    std::atomic<bool> connected{}, stop{};
    std::thread peer([&] {
        // select keeps teardown bounded even if the client fails before connect.
        while (!stop)
        {
            fd_set fds; FD_ZERO(&fds); FD_SET(server, &fds);
            timeval timeout{0, 20000};
            if (select(0, &fds, nullptr, nullptr, &timeout) > 0)
            {
                SOCKET client = accept(server, nullptr, nullptr);
                connected = true;
                while (!stop) Sleep(5);
                closesocket(client);
                break;
            }
        }
    });
    const auto control = std::make_shared<fxprice::RequestControl>(
        cancel ? std::chrono::seconds(10) : std::chrono::milliseconds(500));
    std::thread canceller;
    if (cancel) canceller = std::thread([&] {
        const auto start = GetTickCount64();
        while (!connected && GetTickCount64() - start < 2000) Sleep(5);
        control->Cancel();
    });
    fxprice::WinHttpTransport transport(ntohs(address.sin_port));
    std::string body, error;
    const auto start = GetTickCount64();
    const bool result = transport.GetControlled(L"127.0.0.1", L"/blocked", body, error, control);
    const auto elapsed = GetTickCount64() - start;
    stop = true;
    peer.join();
    if (canceller.joinable()) canceller.join();
    closesocket(server);
    Check(connected && !result && elapsed < 3000, "in-flight WinHTTP abort is bounded");
    Check(error.find(cancel ? "取消" : "整轮") != std::string::npos, "cancel/deadline classified");
}
}
int main()
{
    g_moduleInstance = GetModuleHandleW(nullptr);
    WSADATA data{}; WSAStartup(MAKEWORD(2, 2), &data);
    TestBlockedHttp(true);
    TestBlockedHttp(false);
    WSACleanup();

    SlowProTransport slow;
    const auto budget = std::make_shared<fxprice::RequestControl>(std::chrono::milliseconds(120));
    fxprice::MarketClient pro(slow, fxprice::QuoteSource::BitgetPro, budget);
    std::unordered_map<std::string, fxprice::Quote> quotes;
    std::string error;
    Check(!pro.Fetch({L"EURUSD", L"USDJPY", L"GBPUSD"}, quotes, error) &&
              quotes.empty() && slow.requests == 2 && error.find("整轮") != std::string::npos,
          "Pro symbols share a single deadline, no partial quotes");
    const auto cancelled = std::make_shared<fxprice::RequestControl>();
    cancelled->Cancel();
    fxprice::MarketClient cancelledClient(slow, fxprice::QuoteSource::BitgetPro, cancelled);
    Check(!cancelledClient.Fetch({L"EURUSD"}, quotes, error) && slow.requests == 2,
          "pre-cancelled request does not invoke transport");

    // Callbacks on monitor/UI threads with immutable pointer lifetime.
    fxprice::FxPricePlugin plugin;
    auto* item = plugin.GetItem(1);
    const wchar_t* pinned = item->GetItemLableText();
    const std::wstring original = pinned;
    std::thread writer([&] {
        for (int i = 0; i < 2000; ++i)
        {
            fxprice::Settings settings;
            settings.symbols[0].code = i % 2 ? L"USDJPY" : L"XAUUSD";
            fxprice::PluginTestAccess::Apply(plugin, settings);
        }
    });
    for (int i = 0; i < 4000; ++i)
    {
        Check(std::wstring(pinned) == original, "ABI pointer survives publications");
        const std::wstring value = item->GetItemValueText();
        const std::wstring sample = item->GetItemValueSampleText();
        const std::wstring tooltip = plugin.GetTooltipInfo();
        Check(!value.empty() && !sample.empty() && tooltip.size() <= 384, "concurrent snapshot read");
    }
    writer.join();
    fxprice::Settings settings;
    fxprice::PluginTestAccess::Apply(plugin, settings);
    fxprice::PluginTestAccess::SeedStale(plugin);
    fxprice::PluginTestAccess::ApplyPending(plugin);
    Check(std::wstring(item->GetItemValueText()) == L"--", "stale generation rejected atomically");

    BlockingTransport blocked;
    fxprice::FxPricePlugin active(&blocked);
    fxprice::PluginTestAccess::Start(active);
    if (fxprice::PluginTestAccess::Running(active))
    {
        const auto start = GetTickCount64();
        while (!blocked.entered && GetTickCount64() - start < 2000) Sleep(5);
        fxprice::PluginTestAccess::Apply(active, settings);
        while (fxprice::PluginTestAccess::Running(active) && GetTickCount64() - start < 3000) Sleep(5);
        Check(blocked.cancelled && !fxprice::PluginTestAccess::Running(active), "settings cancel old request");
    }
    active.Shutdown();
    BlockingTransport shutdownTransport;
    fxprice::FxPricePlugin shuttingDown(&shutdownTransport);
    fxprice::PluginTestAccess::Start(shuttingDown);
    const auto shutdownStart = GetTickCount64();
    while (!shutdownTransport.entered && GetTickCount64() - shutdownStart < 2000) Sleep(5);
    shuttingDown.Shutdown();
    while (fxprice::PluginTestAccess::Running(shuttingDown) &&
           GetTickCount64() - shutdownStart < 3000) Sleep(5);
    Check(shutdownTransport.cancelled && !fxprice::PluginTestAccess::Running(shuttingDown),
          "explicit shutdown aborts in-flight request outside loader lock");
    if (!failures) std::cout << "Runtime tests passed\n";
    return failures ? 1 : 0;
}
