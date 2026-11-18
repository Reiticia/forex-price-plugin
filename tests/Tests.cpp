#include "BiquoteClient.h"
#include "Config.h"
#include "MarketData.h"
#include "MarketSchedule.h"
#include "Quote.h"
#include "Tooltip.h"

#include <Windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

class FakeTransport final : public fxprice::IHttpTransport
{
public:
    bool Get(const std::wstring& host, const std::wstring& path,
             std::string& response, std::string& error) override
    {
        ++requests;
        lastHost = host;
        lastPath = path;
        const auto found = responses.find(path);
        response = found == responses.end() ? responseBody : found->second;
        error = succeeds ? std::string{} : "模拟 HTTP 或网络错误";
        return succeeds;
    }

    std::wstring lastHost;
    std::wstring lastPath;
    std::string responseBody;
    std::unordered_map<std::wstring, std::string> responses;
    bool succeeds{true};
    int requests{};
};

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}
} // namespace

int main()
{
    Check(!fxprice::IsWeekendUtc3(5, 20), "UTC 周五 21:00 前仍可刷新");
    Check(fxprice::IsWeekendUtc3(5, 21), "UTC 周五 21:00 起进入 UTC+3 周六");
    Check(fxprice::IsWeekendUtc3(5, 23), "UTC 周五晚间必须暂停");
    Check(fxprice::IsWeekendUtc3(6, 0) && fxprice::IsWeekendUtc3(6, 20) &&
              fxprice::IsWeekendUtc3(6, 21) && fxprice::IsWeekendUtc3(6, 23),
          "UTC 周六全天暂停，跨入 UTC+3 周日仍暂停");
    Check(fxprice::IsWeekendUtc3(0, 0) && fxprice::IsWeekendUtc3(0, 20),
          "UTC 周日 21:00 前仍暂停");
    Check(!fxprice::IsWeekendUtc3(0, 21) && !fxprice::IsWeekendUtc3(0, 23),
          "UTC 周日 21:00 起进入 UTC+3 周一并恢复");
    for (unsigned day = 1; day <= 4; ++day)
    {
        Check(!fxprice::IsWeekendUtc3(day, 0) && !fxprice::IsWeekendUtc3(day, 20) &&
                  !fxprice::IsWeekendUtc3(day, 21) && !fxprice::IsWeekendUtc3(day, 23),
              "UTC 周一至周四保持正常刷新");
    }

    Check(fxprice::NormalizeSymbol(L" eur/usd ") == L"EURUSD",
          "规范化斜杠分隔的外汇标的");
    Check(fxprice::NormalizeSymbol(L"gbp-usd") == L"GBPUSD",
          "规范化横杠分隔的外汇标的");
    Check(fxprice::NormalizeSymbol(L"EURUSD.s") == L"EURUSD",
          "规范化显示后缀的外汇标的");
    Check(fxprice::DisplaySymbol(L"EURUSD") == L"EURUSD.s",
          "按任务栏格式显示外汇标的");
    Check(fxprice::IsValidSymbol(L"USDJPY"), "接受六位外汇标的");
    Check(!fxprice::IsValidSymbol(L"EUR_USD"), "拒绝未规范化标的");

    const std::vector<fxprice::SymbolConfig> normalized = fxprice::NormalizeSymbols(
        {fxprice::SymbolConfig{L"EURUSD"}, fxprice::SymbolConfig{L"eur/usd"},
         fxprice::SymbolConfig{L"EURUSD"}, fxprice::SymbolConfig{L"GBP-USD"}});
    Check(normalized.size() == 2 && normalized[0].code == L"EURUSD" &&
              normalized[1].code == L"GBPUSD",
          "规范化并去重标的列表");
    Check(fxprice::NormalizeSymbols(
              {fxprice::SymbolConfig{L"EURUSD"}, fxprice::SymbolConfig{L"GBPUSD"},
               fxprice::SymbolConfig{L"USDJPY"}, fxprice::SymbolConfig{L"AUDUSD"},
               fxprice::SymbolConfig{L"USDCAD"}, fxprice::SymbolConfig{L"USDCHF"},
               fxprice::SymbolConfig{L"NZDUSD"}, fxprice::SymbolConfig{L"EURGBP"},
               fxprice::SymbolConfig{L"EURJPY"}, fxprice::SymbolConfig{L"GBPJPY"}})
              .size() == 8,
          "标的列表最多保留 8 个");
    Check(fxprice::NormalizeSymbols(
              {fxprice::SymbolConfig{L"!!"}, fxprice::SymbolConfig{L""},
               fxprice::SymbolConfig{L"EUR"}}).empty(),
          "全部无效时返回空列表");

    const std::string query = fxprice::BuildLatestQuery({L"EURUSD", L"USDJPY"});
    Check(query == "/api/latest?symbols=EURUSD&symbols=USDJPY",
          "批量查询按重复 symbols 参数构造");
    Check(fxprice::BuildLatestQuery({L"eur/usd", L"GBP-USD"}) ==
              "/api/latest?symbols=EURUSD&symbols=GBPUSD",
          "批量查询自动规范化标的");
    Check(fxprice::BuildLatestQuery({L"!!", L""}).find("symbols=") ==
              std::string::npos,
          "批量查询跳过无效标的");

    // 截取自 biquote.io /api/latest 的真实响应（字段值保持原样）。
    const std::string response = R"json({
      "EURUSD": {"symbol":"EURUSD","bid":1.15823,"ask":1.15829,"last":0.0,"volume":0,
        "timestamp":"2026-08-28T20:58:48Z","source":"MetaTrader 5 (Broker 1)",
        "high":1.16597,"low":1.15774,"direction":"UP","dayDiffPercent":-0.6075,
        "description":"Euro vs US Dollar","time":"2026.08.28 20:58:48",
        "spread":0.00006,"mid":1.15826,"stale":true,"quoteAgeSeconds":121619,
        "marketState":"closed","lastQuoteAt":"2026-08-28T20:58:48Z"},
      "USDJPY": {"symbol":"USDJPY","bid":160.097,"ask":160.109,"last":0.0,"volume":0,
        "timestamp":"2026-08-28T20:58:47Z","source":"MetaTrader 5 (Broker 1)",
        "high":160.42,"low":159.65,"direction":"UP","dayDiffPercent":0.4911,
        "description":"US Dollar vs Japanese Yen","time":"2026.08.28 20:58:47",
        "spread":0.012,"mid":160.103,"stale":true,"quoteAgeSeconds":121622,
        "marketState":"closed","lastQuoteAt":"2026-08-28T20:58:47Z"}
    })json";
    std::unordered_map<std::string, fxprice::Quote> quotes;
    std::string error;
    Check(fxprice::ParseLatestResponse(response, quotes, error),
          "解析 biquote /api/latest 响应");
    Check(quotes.size() == 2, "解析全部外汇报价");
    Check(std::abs(quotes["EURUSD"].bid - 1.15823) < 0.0000001,
          "读取买一价");
    Check(std::abs(quotes["EURUSD"].ask - 1.15829) < 0.0000001,
          "读取卖一价");
    Check(std::abs(quotes["EURUSD"].midpoint - 1.15826) < 0.0000001,
          "读取服务端中间价");
    Check(quotes["EURUSD"].precision == 5, "按报价小数位保留精度");
    Check(quotes["USDJPY"].precision == 3, "日元货币对保留三位小数");
    Check(quotes["EURUSD"].dailyChangePercent &&
              std::abs(*quotes["EURUSD"].dailyChangePercent + 0.6075) < 0.0000001,
          "读取日涨跌幅");
    Check(quotes["EURUSD"].description == "Euro vs US Dollar",
          "读取标的描述");
    fxprice::DisplayOptions displayAll;
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], displayAll, -1) ==
              L"◀1.15823 ▶1.15829 ≈1.15826 Δ-0.61%",
          "格式化完整买卖行情及日涨跌幅");
    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"], displayAll, -1) ==
              L"1.15826 -0.61%",
          "格式化汇总行情及日涨跌幅");
    Check(fxprice::FormatQuoteSample(displayAll, 5) ==
              L"◀8.88888 ▶8.88888 ≈8.88888 Δ+88.88%",
          "完整宽度示例使用与任务栏一致的符号");
    Check(fxprice::FormatQuoteDetail(quotes["EURUSD"], displayAll, -1) ==
              L"Euro vs US Dollar | 买一 1.15823 | 卖一 1.15829 | 中间价 1.15826 | 日涨跌 -0.61%",
          "工具提示保留标的描述与完整中文字段");

    // 仅显示中间价和涨跌幅，价格固定两位小数。
    fxprice::DisplayOptions partial;
    partial.showBid = false;
    partial.showAsk = false;
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], partial, 2) ==
              L"≈1.16 Δ-0.61%",
          "按勾选字段与固定精度格式化");
    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"], partial, 2) == L"1.16 -0.61%",
          "汇总含中间价和日涨跌幅");
    // 仅显示买价，保留三位小数。
    fxprice::DisplayOptions bidOnly;
    bidOnly.showAsk = false;
    bidOnly.showMid = false;
    bidOnly.showChange = false;
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], bidOnly, 3) == L"◀1.158",
          "仅显示买价并按三位小数格式化");
    // 固定 0 位（整数显示）。
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], bidOnly, 0) == L"◀1",
          "固定 0 位时价格取整显示");

    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"], bidOnly, 3) == L"1.158",
          "关闭涨跌幅后汇总仅显示价格");
    Check(fxprice::FormatQuoteSample(partial, 2) == L"≈8.88 Δ+88.88%",
          "示例为涨跌幅预留宽度");
    Check(fxprice::FormatQuoteSample(bidOnly, 3) == L"◀8.888",
          "关闭涨跌幅后示例不预留百分比");

    Check(fxprice::FormatQuoteSample(bidOnly, -1, L"USDJPY") == L"◀888.888",
          "日元对自动宽度预留三位整数及三位小数");
    Check(fxprice::FormatQuoteSample(bidOnly, -1, L"XAUUSD") == L"◀8888.88" &&
              fxprice::FormatQuoteSample(bidOnly, 5, L"XAUUSD") == L"◀8888.88888",
          "黄金量级与显式配置精度分别生效");
    Check(fxprice::FormatQuoteSample(bidOnly, 0, L"EURUSD") == L"◀8" &&
              fxprice::FormatQuoteSample(bidOnly, 10, L"EURUSD") == L"◀8.8888888888",
          "紧凑样本支持 0 到 10 位固定精度");
    Check(fxprice::FormatQuoteSample(bidOnly, -1, L"USDTRY") == L"◀88.88888" &&
              fxprice::FormatQuoteSample(bidOnly, -1, L"USDIDR") == L"◀88888.88888",
          "较大数值货币对不能统一按一位整数预留");
    Check(fxprice::FormatQuoteSummarySample(displayAll, -1, L"EURUSD") == L"8.88888 +88.88%" &&
              fxprice::FormatQuoteSummarySample(bidOnly, -1, L"USDJPY") == L"888.888",
          "汇总项沿用标的量级与字段开关，不再全局预留五位整数");

    fxprice::DisplayOptions changeOnly;
    changeOnly.showBid = false;
    changeOnly.showAsk = false;
    changeOnly.showMid = false;
    fxprice::Quote changeQuote = quotes["EURUSD"];
    for (const auto& sample : {std::make_pair(0.5, L"+0.50%"),
                               std::make_pair(0.0, L"+0.00%"),
                               std::make_pair(-0.5, L"-0.50%")})
    {
        changeQuote.dailyChangePercent = sample.first;
        Check(fxprice::FormatQuoteValue(changeQuote, changeOnly, 0) ==
                  std::wstring(L"Δ") + sample.second &&
                  fxprice::FormatQuoteSummary(changeQuote, changeOnly, 0) == sample.second,
              "仅显示涨跌幅时保留正负号和两位小数，无多余分隔");
    }
    changeQuote.dailyChangePercent.reset();
    Check(fxprice::FormatQuoteValue(changeQuote, changeOnly, -1) == L"Δ--" &&
              fxprice::FormatQuoteSummary(changeQuote, changeOnly, -1) == L"--",
          "缺少涨跌幅时显示占位符");
    Check(fxprice::FormatQuoteSample(changeOnly, -1) == L"Δ+88.88%",
          "仅显示涨跌幅时示例无多余空格");

    // 详情尾部悬空分隔：全部取消勾选时为空，仅显示买价时以买价结尾。
    fxprice::DisplayOptions detailOff;
    detailOff.showBid = false;
    detailOff.showAsk = false;
    detailOff.showMid = false;
    detailOff.showChange = false;
    Check(fxprice::FormatQuoteDetail(quotes["EURUSD"], detailOff, -1).empty(),
          "全部取消勾选时详情为空");
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], detailOff, -1).empty() &&
              fxprice::FormatQuoteSummary(quotes["EURUSD"], detailOff, -1).empty() &&
              fxprice::FormatQuoteSample(detailOff, -1).empty(),
          "全部取消勾选时任务栏文本和示例为空");
    fxprice::DisplayOptions detailOne;
    detailOne.showAsk = false;
    detailOne.showMid = false;
    detailOne.showChange = false;
    const std::wstring detailText =
        fxprice::FormatQuoteDetail(quotes["EURUSD"], detailOne, -1);
    Check(detailText.find(L"Euro vs US Dollar |") != std::wstring::npos &&
              detailText.size() >= 7 &&
              detailText.compare(detailText.size() - 7, 7, L"1.15823") == 0,
          "仅显示买价时不产生尾部悬空分隔");

    const std::string noMid = R"({"EURUSD":{"symbol":"EURUSD","bid":1.10,"ask":1.12,
        "dayDiffPercent":0.5,"description":"Euro vs US Dollar"}})";
    quotes.clear();
    Check(fxprice::ParseLatestResponse(noMid, quotes, error),
          "解析缺少 mid 字段的响应");
    Check(std::abs(quotes["EURUSD"].midpoint - 1.11) < 0.0000001,
          "缺少 mid 时按买卖价计算中间价");
    Check(!fxprice::ParseLatestResponse(
              R"({"EURUSD":{"symbol":"EURUSD","bid":null,"ask":1.12}})",
              quotes, error),
          "拒绝缺少买价的响应");
    Check(!fxprice::ParseLatestResponse(R"([])", quotes, error),
          "拒绝非对象响应");
    Check(!fxprice::ParseLatestResponse(
              R"({"EURUSD":{"symbol":"EURUSD","bid":0,"ask":0}})",
              quotes, error),
          "拒绝无效零价响应");
    quotes.clear();
    Check(fxprice::ParseLatestResponse(
              R"({"EURUSD":{"symbol":"EURUSD","bid":1.10,"ask":1.12},
                  "INVALIDXYZ":{"symbol":"INVALIDXYZ","bid":1.0,"ask":1.1}})",
              quotes, error) && quotes.size() == 1,
          "跳过服务端返回的非法标的");

    FakeTransport transport;
    transport.responseBody = response;
    fxprice::BiquoteClient client(transport);
    quotes.clear();
    Check(client.Fetch({L"EURUSD", L"USDJPY"}, quotes, error),
          "通过注入传输层获取行情");
    Check(transport.lastHost == L"biquote.io",
          "请求 biquote.io 主机");
    Check(transport.lastPath == L"/api/latest?symbols=EURUSD&symbols=USDJPY",
          "请求批量行情路径");
    Check(quotes.size() == 2, "客户端解析批量行情");
    quotes.clear();
    Check(!client.Fetch({L"!!", L""}, quotes, error) &&
              error.find("没有有效的交易对") != std::string::npos,
          "全无效标的时不发请求并报错");

    Check(client.ValidateSymbol(L" eur/usd.s ", error) && error.empty(),
          "规范化后校验目标标的的有效报价");
    Check(transport.lastPath == L"/api/latest?symbols=EURUSD",
          "添加校验只查询目标标的");
    Check(!client.ValidateSymbol(L"ABCDEF", error) && !error.empty(),
          "其他标的有报价不能使不存在的目标标的通过校验");
    const int requestsBeforeInvalid = transport.requests;
    Check(!client.ValidateSymbol(L"!!", error) &&
              transport.requests == requestsBeforeInvalid,
          "非法格式不发起校验请求");
    for (const std::string& invalidResponse : {
             std::string("{}"), std::string("not json"),
             std::string(R"({"EURUSD":null})"),
             std::string(R"({"EURUSD":{"bid":0,"ask":0}})"),
             std::string(R"({"EURUSD":{"bid":1.1}})"),
             std::string(R"({"EURUSD":{"bid":null,"ask":1.2}})"),
             std::string(R"({"EURUSD":{"bid":-1,"ask":1.2}})")})
    {
        transport.responseBody = invalidResponse;
        Check(!client.ValidateSymbol(L"EURUSD", error) && !error.empty(),
              "空响应、无效 JSON、缺失或非法报价阻止添加");
    }
    transport.succeeds = false;
    transport.responseBody = response;
    Check(!client.ValidateSymbol(L"EURUSD", error) &&
              error == "模拟 HTTP 或网络错误",
          "网络或 HTTP 失败时拒绝添加并保留错误原因");
    transport.succeeds = true;
    Check(client.ValidateSymbol(L"USDJPY", error) && error.empty(),
          "失败后可重新校验，休市时的有效历史买卖报价也可通过");

    wchar_t temporaryPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temporaryPath);
    // 使用进程 ID 隔离测试目录，避免 x64 与 Win32 并行验证时互相删除配置。
    const std::filesystem::path testRoot =
        std::filesystem::path(temporaryPath) /
        (L"FxPricePluginTests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(testRoot);
    const std::filesystem::path configDirectory = testRoot / L"Current";
    std::filesystem::create_directories(configDirectory);
    fxprice::ConfigStore config;
    config.SetDirectory(configDirectory.wstring());
    fxprice::Settings settings;
    settings.source = fxprice::QuoteSource::BitgetPro;
    settings.refreshSeconds = 17;
    settings.symbols = {fxprice::SymbolConfig{L"EURUSD", 4},
                        fxprice::SymbolConfig{L"USDJPY", -1}};
    settings.display.showBid = false;
    settings.display.showChange = false;
    Check(config.Save(settings), "保存无凭据插件配置");
    const fxprice::Settings loaded = config.Load();
    Check(loaded.refreshSeconds == 17, "读取刷新间隔");
    Check(loaded.source == fxprice::QuoteSource::BitgetPro, "保存及恢复数据源模式");
    WritePrivateProfileStringW(L"General", L"Source", L"unknown", config.Path().c_str());
    Check(config.Load().source == fxprice::QuoteSource::Biquote, "未知数据源兼容回退 biquote");
    Check(loaded.symbols.size() == 2 &&
              loaded.symbols[0].code == L"EURUSD" &&
              loaded.symbols[0].precision == 4 &&
              loaded.symbols[1].code == L"USDJPY" &&
              loaded.symbols[1].precision == -1,
          "读取每标的代码与独立小数位");
    Check(!loaded.display.showBid && loaded.display.showAsk && loaded.display.showMid &&
              !loaded.display.showChange,
          "读取显示开关");
    // 手写脏值回退自动，而不是被解析为固定 0 位。
    WritePrivateProfileStringW(L"Symbols", L"Precision0", L"abc",
                               config.Path().c_str());
    const fxprice::Settings dirty = config.Load();
    Check(dirty.symbols[0].precision == -1 &&
              dirty.symbols[1].precision == -1,
          "脏精度键回退自动且不影响其他标的");
    // 旧版只有 General.DisplayPrecision 时，迁移为所有交易对的回退精度。
    const std::filesystem::path legacyDirectory = testRoot / L"Legacy";
    std::filesystem::create_directories(legacyDirectory);
    fxprice::ConfigStore legacyConfig;
    legacyConfig.SetDirectory(legacyDirectory.wstring());
    WritePrivateProfileStringW(L"General", L"Count", L"2",
                               legacyConfig.Path().c_str());
    WritePrivateProfileStringW(L"General", L"DisplayPrecision", L"3",
                               legacyConfig.Path().c_str());
    WritePrivateProfileStringW(L"Symbols", L"Symbol0", L"EURUSD",
                               legacyConfig.Path().c_str());
    WritePrivateProfileStringW(L"Symbols", L"Symbol1", L"USDJPY",
                               legacyConfig.Path().c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, legacyConfig.Path().c_str());
    const fxprice::Settings migrated = legacyConfig.Load();
    Check(migrated.symbols.size() == 2 &&
              migrated.symbols[0].precision == 3 &&
              migrated.symbols[1].precision == 3,
          "旧版 DisplayPrecision 迁移为交易对精度回退");
    std::filesystem::remove_all(legacyDirectory);

    std::ifstream configFile(std::filesystem::path(config.Path()), std::ios::binary);
    const std::string configText((std::istreambuf_iterator<char>(configFile)),
                                 std::istreambuf_iterator<char>());
    Check(configText.find("Token") == std::string::npos &&
              configText.find("AccountId") == std::string::npos,
          "配置不包含账户或令牌字段");
    configFile.close();
    std::filesystem::remove_all(testRoot);

    const auto shortTooltip = fxprice::BuildCompactTooltip({L"EURUSD.s：≈1.10000 Δ+0.50%"}, false);
    Check(shortTooltip.find(L"EURUSD.s：≈1.10000 Δ+0.50%") != std::wstring::npos &&
              shortTooltip.find(L"非24h") != std::wstring::npos &&
              shortTooltip.find(L"更多见插件设置") == std::wstring::npos,
          "短提示保留价格与日涨跌，且明确非 24 小时涨跌");
    const std::size_t contentLimit = fxprice::kMaxTooltipLength - std::wstring(L"\n…（更多见插件设置）").size();
    const auto title = fxprice::BuildCompactTooltip({}, false);
    Check(fxprice::BuildCompactTooltip({std::wstring(contentLimit - title.size() - 1, L'X')}, false).size() ==
              contentLimit, "提示长度边界允许完整一行");
    for (bool weekend : {false, true})
    {
        for (const auto& lines : {
                 std::vector<std::wstring>(fxprice::kMaxSymbols, std::wstring(140, L'X')),
                 std::vector<std::wstring>{std::wstring(5000, L'E')},
                 std::vector<std::wstring>{}})
        {
            const auto tooltip = fxprice::BuildCompactTooltip(lines, weekend);
            Check(tooltip.size() <= fxprice::kMaxTooltipLength,
                  "8 个标的、超长错误、空列表及周末状态均有长度上限");
            Check(tooltip.size() + 384 + 256 <= 1024,
                  "为 CryptoPrice 的 384 字符及宿主 256 字符预留空间");
            Check((tooltip.find(L"周末休市") != std::wstring::npos) == weekend,
                  "截断时保留并正确显示周末暂停状态");
            if (!lines.empty())
                Check(tooltip.find(L"更多见插件设置") != std::wstring::npos,
                      "超限时在完整行边界截断并显示提示");
        }
    }

    using fxprice::QuoteSource;
    Check(fxprice::NormalizeSymbol(L"xau/usd.pro") == L"XAUUSD" &&
              fxprice::DisplaySymbol(L"XAUUSD", QuoteSource::BitgetPro) == L"XAUUSD.pro" &&
              fxprice::DisplaySymbol(L"XAUUSD", QuoteSource::BitgetEcn) == L"XAUUSD",
          "Bitget 模式决定后缀，不混用点差");
    const std::string cfd = R"({"code":"00000","data":[
        {"symbol":"EURUSD","bid1":"1.09890","ask1":"1.09891","bidOpenPriceChange":"-0.00083","askOpenPriceChange":"0.25"},
        {"symbol":"EURUSD.s","bid1":"1.09888","ask1":"1.09899","bidOpenPriceChange":"-0.00080"},
        {"symbol":"EURUSD.pro","bid1":"1.09893","ask1":"1.09893","bidOpenPriceChange":""},
        {"symbol":"XAUUSD","bid1":"3804.13","ask1":"3804.25"}]})";
    Check(fxprice::ParseBitgetCfdResponse(cfd, QuoteSource::BitgetEcn, quotes, error) &&
              quotes.size() == 2 && quotes.at("EURUSD").precision == 5 &&
              std::abs(quotes.at("EURUSD").midpoint - 1.098905) < 1e-9 &&
              std::abs(*quotes.at("EURUSD").dailyChangePercent + 0.083) < 1e-9,
          "CFD ECN 买卖/均值/精度及买价日涨跌比率乘 100");
    Check(fxprice::ParseBitgetCfdResponse(cfd, QuoteSource::BitgetStandard, quotes, error) &&
              quotes.size() == 1 && std::abs(quotes.at("EURUSD").bid - 1.09888) < 1e-9,
          "只使用精确匹配的 .s 模式报价");
    Check(fxprice::ParseBitgetCfdResponse(cfd, QuoteSource::BitgetPro, quotes, error) &&
              !quotes.at("EURUSD").dailyChangePercent,
          "Pro 缺少日涨跌显示未知，不伪造 0%");
    for (const auto& body : {std::string("[]"), std::string("{bad"),
            std::string(R"({"code":"40001","data":[]})"),
            std::string(R"({"code":"00000","data":{}})"),
            std::string(R"({"code":"00000","data":[{"symbol":"EURUSD.s","bid1":"1","ask1":"2"}]})"),
            std::string(R"({"code":"00000","data":[{"symbol":"EURUSD","bid1":"2","ask1":"1"}]})"),
            std::string(R"({"code":"00000","data":[{"symbol":"EURUSD","bid1":"NaN","ask1":"2"}]})"),
            std::string(R"({"code":"00000","data":[{"symbol":"EURUSD","bid1":"0","ask1":"2"}]})")})
        Check(!fxprice::ParseBitgetCfdResponse(body, QuoteSource::BitgetEcn, quotes, error) &&
                  quotes.empty() && !error.empty(), "拒绝错误/非法价格/跨模式回退");
    transport.responseBody = cfd;
    fxprice::MarketClient bitget(transport, QuoteSource::BitgetEcn);
    Check(bitget.Fetch({L"EURUSD", L"XAUUSD"}, quotes, error) && quotes.size() == 2 &&
              transport.lastHost == L"api.bitget.com" &&
              transport.lastPath == L"/api/v3/cfd/market/tickers", "Bitget 多标的一次批量请求");
    Check(bitget.ValidateSymbol(L"EURUSD", error) &&
              transport.lastPath == L"/api/v3/cfd/market/tickers?symbol=EURUSD",
          "Bitget 单标的定向校验");
    Check(!bitget.ValidateSymbol(L"ABCDEF", error), "不能用其他标的代替验证目标");
    fxprice::MarketClient pro(transport, QuoteSource::BitgetPro);
    Check(pro.ValidateSymbol(L"EURUSD.pro", error) &&
              transport.lastPath == L"/api/v3/cfd/market/tickers?symbol=EURUSD.pro", "Pro 请求后缀");
    transport.responses[L"/api/v3/cfd/market/tickers?symbol=USDJPY.pro"] =
        R"({"code":"00000","data":[{"symbol":"USDJPY.pro","bid1":"159.45","ask1":"159.46"}]})";
    const int beforePro = transport.requests;
    Check(pro.Fetch({L"EURUSD", L"USDJPY"}, quotes, error) && quotes.size() == 2 &&
              transport.requests == beforePro + 2, "Pro 多标的逐一精确请求，不依赖全量接口");
    transport.responses[L"/api/v3/cfd/market/tickers?symbol=USDJPY.pro"] =
        R"({"code":"40001","data":[]})";
    Check(!pro.Fetch({L"EURUSD", L"USDJPY"}, quotes, error) && quotes.empty(),
          "Pro 后续请求失败不保留本轮部分数据");
    transport.succeeds = false;
    Check(!pro.Fetch({L"EURUSD"}, quotes, error) && quotes.empty(), "网络失败清空输出，无数据源回退");
    const auto bitgetTooltip = fxprice::BuildCompactTooltip({}, false, QuoteSource::BitgetEcn);
    Check(bitgetTooltip.find(L"Bitget CFD ECN") != std::wstring::npos &&
              bitgetTooltip.find(L"买价日涨跌") != std::wstring::npos, "提示准确标注来源及日涨跌口径");

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
