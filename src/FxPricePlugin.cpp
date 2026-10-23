#include "FxPricePlugin.h"

#include "Module.h"
#include "OptionsWindow.h"
#include "Quote.h"
#include "BiquoteClient.h"

#include <Windows.h>

#include <algorithm>
#include <cwchar>
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
    settings.symbols = NormalizeSymbols(settings.symbols);
    if (settings.symbols.empty()) settings.symbols.push_back(SymbolConfig{});
    return settings;
}

std::wstring SampleNumber(int precision)
{
    const int effective = std::clamp(precision >= 0 ? precision : 5, 0, 10);
    std::wstring result = L"88888";
    if (effective > 0)
    {
        result += L'.';
        result.append(static_cast<std::size_t>(effective), L'8');
    }
    return result;
}

// 汇总行示例文本：主价格精度随配置，按勾选预留日涨跌幅宽度。
std::wstring SummarySample(const DisplayOptions& options, int precision)
{
    std::wstring result;
    if (options.showMid || options.showBid || options.showAsk)
        result = SampleNumber(precision);
    if (options.showChange)
    {
        if (!result.empty()) result += L' ';
        result += L"+888.88%";
    }
    return result;
}
} // namespace

int PriceItem::GetItemWidthEx(void* hdc) const
{
    if (!customDraw_ || !hdc) return 0;
    const std::wstring sample = label_ + sample_;
    SIZE size{};
    return GetTextExtentPoint32W(static_cast<HDC>(hdc), sample.c_str(),
                                 static_cast<int>(sample.size()), &size)
        ? size.cx : 0;
}

void PriceItem::DrawItem(void* hdc, int x, int y, int width, int height, bool)
{
    if (!customDraw_ || !hdc || width <= 0 || height <= 0) return;

    HDC deviceContext = static_cast<HDC>(hdc);
    const int saved = SaveDC(deviceContext);
    SetBkMode(deviceContext, TRANSPARENT);

    const FxPricePlugin& plugin = FxPricePlugin::Instance();
    const COLORREF valueColor = plugin.drawingTaskbar_.load()
        ? static_cast<COLORREF>(plugin.valueTextColor_.load())
        : GetTextColor(deviceContext);
    const COLORREF labelColor = plugin.drawingTaskbar_.load()
        ? static_cast<COLORREF>(plugin.labelTextColor_.load())
        : valueColor;

    RECT labelRect{x, y, x + width, y + height};
    SIZE labelSize{};
    GetTextExtentPoint32W(deviceContext, label_.c_str(), static_cast<int>(label_.size()),
                          &labelSize);
    labelRect.right = (std::min)(labelRect.right, labelRect.left + labelSize.cx);
    SetTextColor(deviceContext, labelColor);
    DrawTextW(deviceContext, label_.c_str(), static_cast<int>(label_.size()), &labelRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    const LONG valueLeft = (std::min)(static_cast<LONG>(x) + labelSize.cx,
                                      static_cast<LONG>(x + width));
    RECT valueRect{valueLeft, y, x + width, y + height};
    SetTextColor(deviceContext, valueColor);
    DrawTextW(deviceContext, value_.c_str(), static_cast<int>(value_.size()), &valueRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (saved != 0) RestoreDC(deviceContext, saved);
}

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
    items_[0].sample_ = L"EURUSD.s 88888.88888";
    for (std::size_t index = 0; index < kMaxSymbols; ++index)
    {
        items_[index + 1].id_ = L"FxPriceItem" + std::to_wstring(index);
        items_[index + 1].name_ = L"外汇行情 " + std::to_wstring(index + 1);
        // TrafficMonitor 会按 ItemId 缓存标签。标的会变化，因此独立项由
        // 插件自绘，确保任务栏始终使用当前配置的标的名称。
        items_[index + 1].customDraw_ = true;
    }
    ApplySettings(settings_);
}

// 函数内静态单例的析构通常不会执行（宿主 FreeLibrary 不运行静态对象
// 析构）；卸载安全由 DLL_PROCESS_DETACH 的 Shutdown() 与启动线程前
// 获取的普通模块引用共同保证。此处保留析构仅作防御。
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
        return L"无需账户或 API Key，显示外汇买一价、卖一价、中间价及日涨跌幅";
    case TMI_AUTHOR: return L"Reiticia";
    case TMI_COPYRIGHT: return L"Copyright (C) 2026";
    case TMI_VERSION: return L"0.4.0";
    case TMI_URL: return L"https://github.com/Reiticia/fx-price-plugin";
    default: return L"";
    }
}

void FxPricePlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (!data) return;
    switch (index)
    {
    case EI_CONFIG_DIR:
        if (*data)
        {
            config_.SetDirectory(data);
            ApplySettings(config_.Load());
            StartRefresh(true);
        }
        break;
    case EI_DRAW_TASKBAR_WND:
        drawingTaskbar_ = std::wcstol(data, nullptr, 10) != 0;
        break;
    case EI_LABEL_TEXT_COLOR:
        labelTextColor_ = std::wcstoul(data, nullptr, 10);
        break;
    case EI_VALUE_TEXT_COLOR:
        valueTextColor_ = std::wcstoul(data, nullptr, 10);
        break;
    default:
        break;
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
            const std::wstring display = DisplaySymbol(settings_.symbols[index].code);
            item.name_ = L"外汇行情 " + display;
            item.label_ = display + L": ";
            item.value_ = L"--";
            item.summary_ = L"--";
            item.sample_ = FormatQuoteSample(settings_.display,
                                             settings_.symbols[index].precision);
            if (item.sample_.empty()) item.sample_ = L"--";
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
        const std::string symbol = WideToAscii(settings_.symbols[index].code);
        const auto found = result.quotes.find(symbol);
        const Quote* quote = found == result.quotes.end() ? nullptr : &found->second;
        const std::wstring error = result.success
            ? L"biquote 未返回该标的" : requestError;
        UpdateItem(index, quote, error, settings_.symbols[index].precision);
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
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&g_moduleInstance), &module))
    {
        workerRunning_ = false;
        nextRefresh_ = std::chrono::steady_clock::time_point{};
        return;
    }
    try
    {
        worker_ = std::thread(&FxPricePlugin::RefreshWorker, this,
                              std::move(snapshot), generation, module);
    }
    catch (...)
    {
        FreeLibrary(module);
        workerRunning_ = false;
        nextRefresh_ = std::chrono::steady_clock::time_point{};
    }
}

void FxPricePlugin::RefreshWorker(Settings settings, unsigned long long generation,
                                  HMODULE module)
{
    PendingResult result;
    result.generation = generation;
    try
    {
        std::vector<std::wstring> codes;
        codes.reserve(settings.symbols.size());
        for (const SymbolConfig& symbol : settings.symbols)
            codes.push_back(symbol.code);
        WinHttpTransport transport;
        BiquoteClient client(transport);
        result.success = client.Fetch(codes, result.quotes, result.error);
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
    FreeLibraryAndExitThread(module, 0);
}

void FxPricePlugin::UpdateItem(std::size_t index, const Quote* quote,
                               const std::wstring& error, int precision)
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
    item.value_ = FormatQuoteValue(*quote, options, precision);
    item.summary_ = FormatQuoteSummary(*quote, options, precision);
    item.detail_ = FormatQuoteDetail(*quote, options, precision);
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
        const std::wstring display = DisplaySymbol(settings_.symbols[index].code);
        if (index != 0)
        {
            aggregate.value_ += L" | ";
            aggregate.sample_ += L" | ";
        }
        aggregate.value_ += display + L" " + item.summary_;
        std::wstring sample = SummarySample(settings_.display,
                                            settings_.symbols[index].precision);
        if (sample.empty()) sample = L"--";
        aggregate.sample_ += display + L" " + sample;
        tooltip_ += L"\n" + display + L"：" + item.detail_;
    }
    tooltip_ += L"\n涨跌幅为相对前收的日涨跌，并非严格滚动 24 小时。";
}
} // namespace fxprice
