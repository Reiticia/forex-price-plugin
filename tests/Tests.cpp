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
    Check(fxprice::IsValidSymbol(L"USDJPY"), "接受六位外汇标的");
    Check(!fxprice::IsValidSymbol(L"EUR_USD"), "拒绝未规范化标的");

    const std::vector<std::wstring> normalized = fxprice::NormalizeSymbols(
        {L"EURUSD", L"eur/usd", L"EURUSD", L"GBP-USD"});
    Check(normalized == std::vector<std::wstring>{L"EURUSD", L"GBPUSD"},
          "规范化并去重标的列表");
    Check(fxprice::NormalizeSymbols({L"EURUSD", L"GBPUSD", L"USDJPY", L"AUDUSD",
                                     L"USDCAD", L"USDCHF", L"NZDUSD", L"EURGBP",
                                     L"EURJPY", L"GBPJPY"})
              .size() == 8,
          "标的列表最多保留 8 个");
    Check(fxprice::NormalizeSymbols({L"!!", L"", L"EUR"}).empty(),
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
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], displayAll) ==
              L"买:1.15823 卖:1.15829 中:1.15826 涨跌:-0.61%",
          "格式化完整买卖行情");
    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"], displayAll) ==
              L"1.15826 -0.61%",
          "格式化汇总行情");
    Check(fxprice::FormatQuoteDetail(quotes["EURUSD"], displayAll).find(L"Euro vs US Dollar") !=
              std::wstring::npos,
          "详情包含标的描述");

    // 仅显示中间价与涨跌幅，并固定两位小数。
    fxprice::DisplayOptions partial;
    partial.showBid = false;
    partial.showAsk = false;
    partial.precision = 2;
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], partial) ==
              L"中:1.16 涨跌:-0.61%",
          "按勾选字段与固定精度格式化");
    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"], partial) == L"1.16 -0.61%",
          "汇总仅含中间价与涨跌幅");
    // 仅显示买价，保留三位小数。
    fxprice::DisplayOptions bidOnly;
    bidOnly.showAsk = false;
    bidOnly.showMid = false;
    bidOnly.showChange = false;
    bidOnly.precision = 3;
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"], bidOnly) == L"买:1.158",
          "仅显示买价并按三位小数格式化");

    // 详情尾部悬空分隔：全部取消勾选时为空，仅显示买价时以买价结尾。
    fxprice::DisplayOptions detailOff;
    detailOff.showBid = false;
    detailOff.showAsk = false;
    detailOff.showMid = false;
    detailOff.showChange = false;
    Check(fxprice::FormatQuoteDetail(quotes["EURUSD"], detailOff).empty(),
          "全部取消勾选时详情为空");
    fxprice::DisplayOptions detailOne;
    detailOne.showAsk = false;
    detailOne.showMid = false;
    detailOne.showChange = false;
    const std::wstring detailText =
        fxprice::FormatQuoteDetail(quotes["EURUSD"], detailOne);
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
    const std::filesystem::path configDirectory =
        std::filesystem::path(temporaryPath) / L"FxPricePluginTests";
    std::filesystem::create_directories(configDirectory);
    fxprice::ConfigStore config;
    config.SetDirectory(configDirectory.wstring());
    fxprice::Settings settings;
    settings.refreshSeconds = 17;
    settings.symbols = {L"EURUSD", L"USDJPY"};
    settings.display.showBid = false;
    settings.display.showChange = false;
    settings.display.precision = 4;
    Check(config.Save(settings), "保存无凭据插件配置");
    const fxprice::Settings loaded = config.Load();
    Check(loaded.refreshSeconds == 17, "读取刷新间隔");
    Check(loaded.symbols == settings.symbols, "读取全部外汇标的");
    Check(!loaded.display.showBid && loaded.display.showAsk && loaded.display.showMid &&
              !loaded.display.showChange,
          "读取显示开关");
    Check(loaded.display.precision == 4, "读取固定小数位");
    std::ifstream configFile(std::filesystem::path(config.Path()), std::ios::binary);
    const std::string configText((std::istreambuf_iterator<char>(configFile)),
                                 std::istreambuf_iterator<char>());
    Check(configText.find("Token") == std::string::npos &&
              configText.find("AccountId") == std::string::npos,
          "配置不包含账户或令牌字段");
    configFile.close();
    std::filesystem::remove_all(configDirectory);

    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
