#include "FxPricePlugin.h"

#include "Module.h"
#include "MarketSchedule.h"
#include "OptionsWindow.h"
#include "Quote.h"
#include "BiquoteClient.h"
#include "Tooltip.h"

#include <Windows.h>

#include <algorithm>
#include <cwchar>
#include <exception>
#include <stdexcept>
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

} // namespace

int PriceItem::GetItemWidthEx(void* hdc) const
{
    if (!customDraw_ || !hdc) return 0;
    const auto view = published_.Load();
    const std::wstring sample = view->label + view->sample;
    SIZE size{}, actualSize{};
    if (!GetTextExtentPoint32W(static_cast<HDC>(hdc), sample.c_str(),
                               static_cast<int>(sample.size()), &size)) return 0;
    // 布局重新测量时，异常大价格/涨跌也不能比已知实际文本窄。
    const std::wstring actual = view->label + view->value;
    GetTextExtentPoint32W(static_cast<HDC>(hdc), actual.c_str(),
                          static_cast<int>(actual.size()), &actualSize);
    return (std::max)(size.cx, actualSize.cx);
}

void PriceItem::DrawItem(void* hdc, int x, int y, int width, int height, bool)
{
    if (!customDraw_ || !hdc || width <= 0 || height <= 0) return;

    const auto view = published_.Load();
    const auto& label = view->label;
    const auto& value = view->value;
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
    GetTextExtentPoint32W(deviceContext, label.c_str(), static_cast<int>(label.size()),
                          &labelSize);
    labelRect.right = (std::min)(labelRect.right, labelRect.left + labelSize.cx);
    SetTextColor(deviceContext, labelColor);
    DrawTextW(deviceContext, label.c_str(), static_cast<int>(label.size()), &labelRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    const LONG valueLeft = (std::min)(static_cast<LONG>(x) + labelSize.cx,
                                      static_cast<LONG>(x + width));
    RECT valueRect{valueLeft, y, x + width, y + height};
    SetTextColor(deviceContext, valueColor);
    DrawTextW(deviceContext, value.c_str(), static_cast<int>(value.size()), &valueRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (saved != 0) RestoreDC(deviceContext, saved);
}

FxPricePlugin& FxPricePlugin::Instance()
{
    static FxPricePlugin instance;
    return instance;
}

FxPricePlugin::FxPricePlugin(IHttpTransport* transport) : transportOverride_(transport)
{
    items_[0].id_ = L"FxPriceSummary";
    items_[0].name_ = L"外汇行情汇总";
    items_[0].label_ = L"外汇: ";
    items_[0].sample_ = L"EURUSD.s 8.88888";
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

// Workers hold a module reference until all C++ objects have unwound.
// No thread join/wait in DLL static destruction or DllMain.
FxPricePlugin::~FxPricePlugin() = default;

void FxPricePlugin::Shutdown()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    shuttingDown_ = true;
    if (request_) request_->Cancel();
}

IPluginItem* FxPricePlugin::GetItem(int index)
{
    if (index < 0) return nullptr;
    std::lock_guard<std::mutex> lock(stateMutex_);
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
        std::lock_guard<std::mutex> lock(stateMutex_);
        draft = settings_;
    }
    if (!OptionsWindow::Show(static_cast<HWND>(parent), draft))
        return OR_OPTION_UNCHANGED;
    draft = NormalizeSettings(std::move(draft));
    bool saved;
    { std::lock_guard<std::mutex> lock(configMutex_); saved = config_.Save(draft); }
    if (!saved)
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
            Settings loaded;
            { std::lock_guard<std::mutex> lock(configMutex_);
              config_.SetDirectory(data); loaded = config_.Load(); }
            ApplySettings(std::move(loaded));
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
        Settings loaded;
        { std::lock_guard<std::mutex> lock(configMutex_);
          config_.SetDirectory(directory); loaded = config_.Load(); }
        ApplySettings(std::move(loaded));
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
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (request_) request_->Cancel();
        ++generation_;
        nextRefresh_ = std::chrono::steady_clock::time_point{};
        settings_ = std::move(settings);
        for (std::size_t index = 0; index < settings_.symbols.size(); ++index)
        {
            PriceItem& item = items_[index + 1];
            const std::wstring display = DisplaySymbol(settings_.symbols[index].code, settings_.source);
            item.name_ = L"外汇行情 " + display;
            item.label_ = display + L": ";
            item.value_ = L"--";
            item.summary_ = L"--";
            item.sample_ = FormatQuoteSample(settings_.display,
                                             settings_.symbols[index].precision,
                                             settings_.symbols[index].code);
            if (item.sample_.empty()) item.sample_ = L"--";
            item.detail_ = L"等待行情";
        }
        items_[0].value_ = L"等待行情";
        RebuildAggregateAndTooltip();
    }
}

void FxPricePlugin::ApplyPendingResult()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!hasPending_) return;
    PendingResult result = std::move(pending_);
    hasPending_ = false;
    if (result.generation != generation_) return;
    const std::wstring requestError = Utf8ToWide(result.error);
    for (std::size_t index = 0; index < settings_.symbols.size(); ++index)
    {
        const std::string symbol = WideToAscii(settings_.symbols[index].code);
        const auto found = result.quotes.find(symbol);
        const Quote* quote = found == result.quotes.end() ? nullptr : &found->second;
        const std::wstring error = result.success
            ? std::wstring(SourceName(settings_.source)) + L" 未返回该标的" : requestError;
        UpdateItem(index, quote, error, settings_.symbols[index].precision);
    }
    RebuildAggregateAndTooltip();
}

struct FxPricePlugin::WorkerContext
{
    FxPricePlugin* plugin;
    Settings settings;
    unsigned long long generation;
    std::shared_ptr<RequestControl> control;
    HMODULE module;
};

DWORD WINAPI FxPricePlugin::WorkerEntry(void* raw)
{
    const HMODULE module = static_cast<WorkerContext*>(raw)->module;
    {
        // The only ExitThread call is AFTER this scope has unwound all C++
        // state, including settings, quotes, transport callbacks and context.
        std::unique_ptr<WorkerContext> context(static_cast<WorkerContext*>(raw));
        try
        {
            context->plugin->RefreshWorker(context->settings, context->generation, context->control);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(context->plugin->stateMutex_);
            context->plugin->workerRunning_ = false;
            if (context->plugin->request_ == context->control)
                context->plugin->request_.reset();
        }
    }
    FreeLibraryAndExitThread(module, 0);
}

void FxPricePlugin::StartRefresh(bool force)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (shuttingDown_) return;

    // 固定 UTC+3，不使用电脑本地时区或夏令时；UTC 21:00 起为下一天。
    // 所有刷新入口（包括强制刷新）都遵守周末暂停。
    SYSTEMTIME utcTime{};
    GetSystemTime(&utcTime);
    const bool weekend = IsWeekendUtc3(utcTime.wDayOfWeek, utcTime.wHour);
    if (weekendPaused_ != weekend)
    {
        weekendPaused_ = weekend;
        RebuildAggregateAndTooltip();
    }
    if (weekend)
    {
        // 不启动请求、不覆盖已有报价；下一个工作日首次回调即可恢复刷新。
        nextRefresh_ = std::chrono::steady_clock::time_point{};
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (force) nextRefresh_ = std::chrono::steady_clock::time_point{};
    if (workerRunning_ || now < nextRefresh_) return;
    nextRefresh_ = now + std::chrono::seconds(settings_.refreshSeconds);
    LaunchWorkerLocked();
}

void FxPricePlugin::LaunchWorkerLocked()
{
    if (workerRunning_ || shuttingDown_) return;
    Settings snapshot = settings_;
    const unsigned long long generation = generation_;
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
        request_ = std::make_shared<RequestControl>();
        auto context = std::make_unique<WorkerContext>(WorkerContext{
            this, std::move(snapshot), generation, request_, module});
        HANDLE thread = CreateThread(nullptr, 0, WorkerEntry, context.get(), 0, nullptr);
        if (!thread) throw std::runtime_error("CreateThread failed");
        context.release();
        CloseHandle(thread);
    }
    catch (...)
    {
        FreeLibrary(module);
        workerRunning_ = false;
        nextRefresh_ = std::chrono::steady_clock::time_point{};
    }
}

void FxPricePlugin::RefreshWorker(const Settings& settings, unsigned long long generation,
                                  const std::shared_ptr<RequestControl>& control)
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
        MarketClient client(transportOverride_ ? *transportOverride_ : transport, settings.source, control);
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

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!shuttingDown_ && generation == generation_)
    {
        pending_ = std::move(result);
        hasPending_ = true;
    }
    workerRunning_ = false;
    if (request_ == control) request_.reset();
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
    std::vector<std::wstring> tooltipLines;
    for (std::size_t index = 0; index < settings_.symbols.size(); ++index)
    {
        const PriceItem& item = items_[index + 1];
        const std::wstring display = DisplaySymbol(settings_.symbols[index].code, settings_.source);
        if (index != 0)
        {
            aggregate.value_ += L" | ";
            aggregate.sample_ += L" | ";
        }
        aggregate.value_ += display + L" " + item.summary_;
        std::wstring sample = FormatQuoteSummarySample(settings_.display,
                                            settings_.symbols[index].precision,
                                            settings_.symbols[index].code);
        if (sample.empty()) sample = L"--";
        aggregate.sample_ += display + L" " + sample;
        // Keep the selected bid/ask/mid/change fields, but don't repeat the
        // provider's unbounded English description on every tooltip row.
        std::wstring line = display + L"：" + item.value_;
        if (item.value_ == L"--" && !item.detail_.empty())
            line += L"（" + item.detail_.substr(0, 40) + (item.detail_.size() > 40 ? L"…" : L"") + L"）";
        tooltipLines.push_back(std::move(line));
    }
    tooltip_ = BuildCompactTooltip(tooltipLines, weekendPaused_, settings_.source);
    for (auto& item : items_) item.Publish();
    publishedTooltip_.Publish(tooltip_);
}
} // namespace fxprice
