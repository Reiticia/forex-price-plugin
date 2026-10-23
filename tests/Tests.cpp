#include "BiquoteClient.h"
#include "Config.h"
#include "MarketData.h"
#include "Quote.h"

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
        lastHost = host;
        lastPath = path;
        response = responseBody;
        error.clear();
        return true;
    }

    std::wstring lastHost;
    std::wstring lastPath;
    std::string responseBody;
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
              L"买:1.15823 卖:1.15829 中:1.15826 涨跌:-0.61%",
          "格式化完整买卖行情及日涨跌幅");
    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"], displayAll, -1) ==
              L"1.15826 -0.61%",
          "格式化汇总行情及日涨跌幅");
    Check(fxprice::FormatQuoteDetail(quotes["EURUSD"], displayAll, -1).find(L"Euro vs US Dollar") !=
              std::wstring::npos,
          "详情包含标的描述");

    // 仅显示中间价和涨跌幅，价格固定两位小数。
    fxprice::DisplayOptions partial;
    partial.showBid = false;
    partial.showAsk = false;
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], partial, 2) ==
              L"中:1.16 涨跌:-0.61%",
          "按勾选字段与固定精度格式化");
    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"], partial, 2) == L"1.16 -0.61%",
          "汇总含中间价和日涨跌幅");
    // 仅显示买价，保留三位小数。
    fxprice::DisplayOptions bidOnly;
    bidOnly.showAsk = false;
    bidOnly.showMid = false;
    bidOnly.showChange = false;
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], bidOnly, 3) == L"买:1.158",
          "仅显示买价并按三位小数格式化");
    // 固定 0 位（整数显示）。
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], bidOnly, 0) == L"买:1",
          "固定 0 位时价格取整显示");

    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"], bidOnly, 3) == L"1.158",
          "关闭涨跌幅后汇总仅显示价格");
    Check(fxprice::FormatQuoteSample(partial, 2) == L"中:88888.88 涨跌:+888.88%",
          "示例为涨跌幅预留宽度");
    Check(fxprice::FormatQuoteSample(bidOnly, 3) == L"买:88888.888",
          "关闭涨跌幅后示例不预留百分比");

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
                  std::wstring(L"涨跌:") + sample.second &&
                  fxprice::FormatQuoteSummary(changeQuote, changeOnly, 0) == sample.second,
              "仅显示涨跌幅时保留正负号和两位小数，无多余分隔");
    }
    changeQuote.dailyChangePercent.reset();
    Check(fxprice::FormatQuoteValue(changeQuote, changeOnly, -1) == L"涨跌:--" &&
              fxprice::FormatQuoteSummary(changeQuote, changeOnly, -1) == L"--",
          "缺少涨跌幅时显示占位符");
    Check(fxprice::FormatQuoteSample(changeOnly, -1) == L"涨跌:+888.88%",
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
    settings.refreshSeconds = 17;
    settings.symbols = {fxprice::SymbolConfig{L"EURUSD", 4},
                        fxprice::SymbolConfig{L"USDJPY", -1}};
    settings.display.showBid = false;
    settings.display.showChange = false;
    Check(config.Save(settings), "保存无凭据插件配置");
    const fxprice::Settings loaded = config.Load();
    Check(loaded.refreshSeconds == 17, "读取刷新间隔");
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

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
