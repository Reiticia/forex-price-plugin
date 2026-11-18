#pragma once

#include "Config.h"
#include "PluginInterface.h"
#include "Quote.h"
#include "PublishedSnapshot.h"
#include "RequestControl.h"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <memory>
#include <unordered_map>
#include <Windows.h>

namespace fxprice
{
class IHttpTransport;
struct PluginTestAccess;

class PriceItem final : public IPluginItem
{
public:
    const wchar_t* GetItemName() const override { return published_.Pin(0)->name.c_str(); }
    const wchar_t* GetItemId() const override { return id_.c_str(); }
    const wchar_t* GetItemLableText() const override { return published_.Pin(1)->label.c_str(); }
    const wchar_t* GetItemValueText() const override { return published_.Pin(2)->value.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return published_.Pin(3)->sample.c_str(); }
    bool IsCustomDraw() const override { return customDraw_; }
    int GetItemWidth() const override { return customDraw_ ? 320 : 0; }
    int GetItemWidthEx(void* hdc) const override;
    void DrawItem(void* hdc, int x, int y, int width, int height, bool) override;

private:
    friend class FxPricePlugin;
    struct View { std::wstring name, label, value, sample; };
    PublishedSnapshot<View, 4> published_;
    void Publish() { published_.Publish({name_, label_, value_, sample_}); }
    std::wstring name_{L"外汇行情"};
    std::wstring id_{L"FxPrice"};
    std::wstring label_{L"外汇: "};
    std::wstring value_{L"--"};
    std::wstring summary_{L"--"};
    std::wstring sample_{L"◀8.88888 ▶8.88888 ≈8.88888"};
    std::wstring detail_{L"等待行情"};
    bool customDraw_{};
};

// DataRequired runs on the host monitor thread, settings/drawing on UI threads.
// stateMutex_ serializes settings, generation, scheduling and mutable display state.
// ABI getters pin immutable published snapshots; drawing loads a single snapshot.
// Workers hold a module reference and only publish pending results under stateMutex_.
class FxPricePlugin final : public ITMPlugin
{
public:
    static FxPricePlugin& Instance();
    void Shutdown();

    explicit FxPricePlugin(IHttpTransport* transport = nullptr);
    ~FxPricePlugin();

    int GetAPIVersion() const override { return 8; }
    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    OptionReturn ShowOptionsDialog(void* parent) override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override { return publishedTooltip_.Pin(0)->c_str(); }
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    void OnInitialize(ITrafficMonitor* application) override;
    int GetCommandCount() override { return 1; }
    const wchar_t* GetCommandName(int commandIndex) override;
    void OnPluginCommand(int commandIndex, void*, void*) override;

private:
    friend class PriceItem;
    friend struct PluginTestAccess;
    IHttpTransport* transportOverride_{}; // non-owning test transport; caller outlives plugin/jobs

    struct PendingResult
    {
        unsigned long long generation{};
        bool success{};
        std::unordered_map<std::string, Quote> quotes;
        std::string error;
    };

    void ApplySettings(Settings settings);
    void ApplyPendingResult();
    void StartRefresh(bool force);
    void LaunchWorkerLocked();
    struct WorkerContext;
    static DWORD WINAPI WorkerEntry(void* context);
    void RefreshWorker(const Settings& settings, unsigned long long generation,
                       const std::shared_ptr<RequestControl>& control);
    void UpdateItem(std::size_t index, const Quote* quote, const std::wstring& error,
                    int precision);
    void RebuildAggregateAndTooltip();

    std::array<PriceItem, kMaxSymbols + 1> items_;
    ConfigStore config_;
    Settings settings_;
    bool weekendPaused_{};
    std::wstring tooltip_{L"biquote.io 外汇行情\n等待刷新"};

    PublishedSnapshot<std::wstring, 1> publishedTooltip_;
    std::mutex stateMutex_;
    std::mutex configMutex_;
    PendingResult pending_;
    bool hasPending_{};
    std::shared_ptr<RequestControl> request_;
    bool workerRunning_{};
    std::atomic<bool> shuttingDown_{};
    unsigned long long generation_{};
    std::atomic<bool> drawingTaskbar_{};
    std::atomic<unsigned long> labelTextColor_{};
    std::atomic<unsigned long> valueTextColor_{};
    std::chrono::steady_clock::time_point nextRefresh_{};
};
} // namespace fxprice
