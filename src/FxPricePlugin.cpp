#include "FxPricePlugin.h"

#include "Module.h"
#include "OptionsWindow.h"
#include "Quote.h"
#include "BiquoteClient.h"

#include <Windows.h>

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

namespace fxprice
{
namespace
{
std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           value.data(), static_cast<int>(value.size()),
                                           nullptr, 0);
    if (length <= 0) return L"行情请求失败";
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        value.data(), static_cast<int>(value.size()),
                        result.data(), length);
    return result;
}

Settings NormalizeSettings(Settings settings)
{
    settings.refreshSeconds = std::clamp(settings.refreshSeconds, 2, 300);
    std::vector<std::wstring> normalized;
    for (const std::wstring& raw : settings.symbols)
    {
        const std::wstring symbol = NormalizeSymbol(raw);
        if (!IsValidSymbol(symbol) ||
            std::find(normalized.begin(), normalized.end(), symbol) != normalized.end())
            continue;
        normalized.push_back(symbol);
        if (normalized.size() == kMaxSymbols) break;
    }
    if (normalized.empty()) normalized.push_back(L"EURUSD");
    settings.symbols = std::move(normalized);
    return settings;
}

// 汇总行示例文本：主价格占位 + （勾选时的）涨跌幅占位，精度随配置。
std::wstring SummarySample(const DisplayOptions& options)
{
    const int precision = options.precision >= 0 ? options.precision : 5;
    std::wstring result;
    if (options.showMid || options.showBid || options.showAsk)
        result = FormatNumber(0.0, precision);
    if (options.showChange)
    {
        if (!result.empty()) result += L' ';
        result += L"+0.00%";
    }
    return result;
}
} // namespace

FxPricePlugin& FxPricePlugin::Instance()
{
    static FxPricePlugin instance;
    return instance;
}

FxPricePlugin::FxPricePlugin()
{
    items_[0].id_ = L"FxPriceSummary";
    items_[0].name_ = L"外汇行情汇总";
    items_[0].label_ = L"外汇: ";
    items_[0].sample_ = L"EUR/USD 0.00000 +00.00%";
    for (std::size_t index = 0; index < kMaxSymbols; ++index)
    {
        items_[index + 1].id_ = L"FxPriceItem" + std::to_wstring(index);
        items_[index + 1].name_ = L"外汇行情 " + std::to_wstring(index + 1);
    }
    ApplySettings(settings_);
}

// 函数内静态单例的析构通常不会执行（宿主 FreeLibrary 不运行静态对象
// 析构）；卸载安全由 DLL_PROCESS_DETACH 的 Shutdown() 与工作线程的
// 模块固定（RefreshWorker）共同保证。此处保留析构仅作防御。
FxPricePlugin::~FxPricePlugin()
{
    shuttingDown_ = true;
    if (worker_.joinable()) worker_.join();
}

void FxPricePlugin::Shutdown()
{
    shuttingDown_ = true;
}

IPluginItem* FxPricePlugin::GetItem(int index)
{
    if (index < 0) return nullptr;
    std::lock_guard<std::mutex> lock(settingsMutex_);
    const std::size_t count = settings_.symbols.size() + 1;
    if (static_cast<std::size_t>(index) >= count ||
        static_cast<std::size_t>(index) >= items_.size())
        return nullptr;
    return &items_[static_cast<std::size_t>(index)];
}

void FxPricePlugin::DataRequired()
{
    ApplyPendingResult();
    StartRefresh(false);
}

ITMPlugin::OptionReturn FxPricePlugin::ShowOptionsDialog(void* parent)
{
    Settings draft;
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        draft = settings_;
    }
    if (!OptionsWindow::Show(static_cast<HWND>(parent), draft))
        return OR_OPTION_UNCHANGED;
    draft = NormalizeSettings(std::move(draft));
    if (!config_.Save(draft))
        MessageBoxW(static_cast<HWND>(parent),
                    L"无法保存 FxPrice.ini，请检查插件配置目录权限。",
                    L"外汇行情", MB_OK | MB_ICONWARNING);
    ApplySettings(std::move(draft));
    StartRefresh(true);
    return OR_OPTION_CHANGED;
}

const wchar_t* FxPricePlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME: return L"外汇行情";
    case TMI_DESCRIPTION:
        return L"无需账户或 API Key，显示外汇买一价、卖一价、中间价和日涨跌幅";
    case TMI_AUTHOR: return L"Reiticia";
    case TMI_COPYRIGHT: return L"Copyright (C) 2026";
    case TMI_VERSION: return L"0.3.0";
    case TMI_URL: return L"https://github.com/Reiticia/fx-price-plugin";
    default: return L"";
    }
}

void FxPricePlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data && *data)
    {
        config_.SetDirectory(data);
        ApplySettings(config_.Load());
        StartRefresh(true);
    }
}

void FxPricePlugin::OnInitialize(ITrafficMonitor* application)
{
    if (!application) return;
    const wchar_t* directory = application->GetPluginConfigDir();
    if (directory && *directory)
    {
        config_.SetDirectory(directory);
        ApplySettings(config_.Load());
    }
}

const wchar_t* FxPricePlugin::GetCommandName(int commandIndex)
{
    return commandIndex == 0 ? L"立即刷新外汇行情" : nullptr;
}

void FxPricePlugin::OnPluginCommand(int commandIndex, void*, void*)
{
    if (commandIndex == 0) StartRefresh(true);
}

void FxPricePlugin::ApplySettings(Settings settings)
{
    settings = NormalizeSettings(std::move(settings));
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        settings_ = std::move(settings);
        for (std::size_t index = 0; index < settings_.symbols.size(); ++index)
        {
            PriceItem& item = items_[index + 1];
            const std::wstring display = DisplaySymbol(settings_.symbols[index]);
            item.name_ = L"外汇行情 " + display;
            item.label_ = display + L": ";
            item.value_ = L"--";
            item.summary_ = L"--";
            item.sample_ = FormatQuoteSample(settings_.display);
            item.detail_ = L"等待行情";
        }
        items_[0].value_ = L"等待行情";
        RebuildAggregateAndTooltip();
    }
    ++generation_;
    std::lock_guard<std::mutex> refreshLock(refreshMutex_);
    nextRefresh_ = std::chrono::steady_clock::time_point{};
}

void FxPricePlugin::ApplyPendingResult()
{
    PendingResult result;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (!hasPending_) return;
        result = std::move(pending_);
        hasPending_ = false;
    }
    if (result.generation != generation_.load()) return;

    const std::wstring requestError = Utf8ToWide(result.error);
    std::lock_guard<std::mutex> lock(settingsMutex_);
    for (std::size_t index = 0; index < settings_.symbols.size(); ++index)
    {
        const std::string symbol = WideToAscii(settings_.symbols[index]);
        const auto found = result.quotes.find(symbol);
        const Quote* quote = found == result.quotes.end() ? nullptr : &found->second;
        const std::wstring error = result.success
            ? L"biquote 未返回该标的" : requestError;
        UpdateItem(index, quote, error);
    }
    RebuildAggregateAndTooltip();
}

void FxPricePlugin::StartRefresh(bool force)
{
    std::lock_guard<std::mutex> refreshLock(refreshMutex_);
    if (shuttingDown_) return;
    const auto now = std::chrono::steady_clock::now();
    if (force) nextRefresh_ = std::chrono::steady_clock::time_point{};
    if (workerRunning_ || now < nextRefresh_) return;
    if (worker_.joinable()) worker_.join();

    Settings snapshot;
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        snapshot = settings_;
    }
    nextRefresh_ = now + std::chrono::seconds(snapshot.refreshSeconds);
    const unsigned long long generation = generation_.load();
    workerRunning_ = true;
    worker_ = std::thread(&FxPricePlugin::RefreshWorker, this,
                          std::move(snapshot), generation);
}

void FxPricePlugin::RefreshWorker(Settings settings, unsigned long long generation)
{
    // 固定本模块引用：宿主可能在后台请求期间卸载 DLL，而静态单例析构
    // 不会执行、DllMain 也无法安全 join 本线程。固定后，即使宿主调用
    // FreeLibrary，模块也会保持加载，直到本线程释放固定并退出。
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&g_moduleInstance), &module);

    PendingResult result;
    result.generation = generation;
    try
    {
        WinHttpTransport transport;
        BiquoteClient client(transport);
        result.success = client.Fetch(settings.symbols, result.quotes, result.error);
    }
    catch (const std::exception& exception)
    {
        result.error = std::string("行情刷新异常: ") + exception.what();
    }
    catch (...)
    {
        result.error = "行情刷新发生未知异常";
    }

    if (!shuttingDown_)
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_ = std::move(result);
        hasPending_ = true;
    }
    workerRunning_ = false;
    if (module) FreeLibraryAndExitThread(module, 0);
    // FreeLibraryAndExitThread 不返回；仅在未取得模块固定时才执行到这里。
}

void FxPricePlugin::UpdateItem(std::size_t index, const Quote* quote,
                               const std::wstring& error)
{
    PriceItem& item = items_[index + 1];
    if (!quote)
    {
        item.value_ = L"--";
        item.summary_ = L"--";
        item.detail_ = error.empty() ? L"行情获取失败" : error;
        return;
    }

    const DisplayOptions& options = settings_.display;
    item.value_ = FormatQuoteValue(*quote, options);
    item.summary_ = FormatQuoteSummary(*quote, options);
    item.detail_ = FormatQuoteDetail(*quote, options);
    if (item.value_.empty()) item.value_ = L"--";
    if (item.summary_.empty()) item.summary_ = L"--";
    if (item.detail_.empty()) item.detail_ = L"--";
}

void FxPricePlugin::RebuildAggregateAndTooltip()
{
    PriceItem& aggregate = items_[0];
    aggregate.value_.clear();
    aggregate.sample_.clear();
    tooltip_ = L"biquote.io 外汇行情";
    for (std::size_t index = 0; index < settings_.symbols.size(); ++index)
    {
        const PriceItem& item = items_[index + 1];
        const std::wstring display = DisplaySymbol(settings_.symbols[index]);
        if (index != 0)
        {
            aggregate.value_ += L" | ";
            aggregate.sample_ += L" | ";
        }
        aggregate.value_ += display + L" " + item.summary_;
        aggregate.sample_ += display + L" " + SummarySample(settings_.display);
        tooltip_ += L"\n" + display + L"：" + item.detail_;
    }
    tooltip_ += L"\n涨跌幅为相对前收的日涨跌，并非严格滚动 24 小时。";
}
} // namespace fxprice
