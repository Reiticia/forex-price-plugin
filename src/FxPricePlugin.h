#pragma once

#include "Config.h"
#include "PluginInterface.h"
#include "Quote.h"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace fxprice
{
class PriceItem final : public IPluginItem
{
public:
    const wchar_t* GetItemName() const override { return name_.c_str(); }
    const wchar_t* GetItemId() const override { return id_.c_str(); }
    const wchar_t* GetItemLableText() const override { return label_.c_str(); }
    const wchar_t* GetItemValueText() const override { return value_.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return sample_.c_str(); }

private:
    friend class FxPricePlugin;
    std::wstring name_{L"外汇行情"};
    std::wstring id_{L"FxPrice"};
    std::wstring label_{L"外汇: "};
    std::wstring value_{L"--"};
    std::wstring summary_{L"--"};
    std::wstring sample_{L"买 0.00000 卖 0.00000 中 0.00000 +00.00%"};
    std::wstring detail_{L"等待行情"};
};

// 线程模型：items_ 与 tooltip_ 只在 TrafficMonitor 的 UI 线程读写（宿主
// 通过 DataRequired / ShowOptionsDialog / OnInitialize 等回调访问），后台
// 刷新线程只通过 pending_（pendingMutex_）回传结果，settings_ 由
// settingsMutex_ 保护。GetItem* / GetTooltipInfo 按宿主约定在 UI 线程调用。
class FxPricePlugin final : public ITMPlugin
{
public:
    static FxPricePlugin& Instance();
    void Shutdown();

    FxPricePlugin();
    ~FxPricePlugin();

    int GetAPIVersion() const override { return 8; }
    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    OptionReturn ShowOptionsDialog(void* parent) override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override { return tooltip_.c_str(); }
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    void OnInitialize(ITrafficMonitor* application) override;
    int GetCommandCount() override { return 1; }
    const wchar_t* GetCommandName(int commandIndex) override;
    void OnPluginCommand(int commandIndex, void*, void*) override;

private:
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
    void RefreshWorker(Settings settings, unsigned long long generation);
    void UpdateItem(std::size_t index, const Quote* quote, const std::wstring& error);
    void RebuildAggregateAndTooltip();

    std::array<PriceItem, kMaxSymbols + 1> items_;
    ConfigStore config_;
    Settings settings_;
    std::wstring tooltip_{L"biquote.io 外汇行情\n等待刷新"};

    std::mutex settingsMutex_;
    std::mutex pendingMutex_;
    std::mutex refreshMutex_;
    PendingResult pending_;
    bool hasPending_{};
    std::thread worker_;
    std::atomic<bool> workerRunning_{};
    std::atomic<bool> shuttingDown_{};
    std::atomic<unsigned long long> generation_{};
    std::chrono::steady_clock::time_point nextRefresh_{};
};
} // namespace fxprice
