#include "Config.h"
#include "MarketData.h"
#include "Quote.h"
#include "ScannerClient.h"

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
    bool PostJson(const std::wstring& host, const std::wstring& path,
                  const std::string& body, std::string& response,
                  std::string& error) override
    {
        lastHost = host;
        lastPath = path;
        lastBody = body;
        response = responseBody;
        error.clear();
        return true;
    }

    std::wstring lastHost;
    std::wstring lastPath;
    std::string lastBody;
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
    Check(fxprice::BuildScannerRequest({L"!!", L""}).find("FX_IDC:") ==
              std::string::npos,
          "请求构造跳过无效标的");

    const std::string request = fxprice::BuildScannerRequest({L"EURUSD", L"USDJPY"});
    Check(request.find("FX_IDC:EURUSD") != std::string::npos,
          "请求包含 EURUSD ticker");
    Check(request.find("FX_IDC:USDJPY") != std::string::npos,
          "请求包含 USDJPY ticker");
    Check(request.find("\"bid\"") != std::string::npos &&
              request.find("\"ask\"") != std::string::npos &&
              request.find("\"change\"") != std::string::npos,
          "请求买价、卖价和日涨跌字段");
    Check(request.find("\"close\"") == std::string::npos,
          "请求不包含未使用的收盘价字段");

    const std::string response = R"({
      "totalCount": 2,
      "data": [
        {"s":"FX_IDC:EURUSD","d":["EURUSD","EURO / U.S. DOLLAR",1.15831,1.15857,-0.5836408892]},
        {"s":"FX_IDC:USDJPY","d":["USDJPY","U.S. DOLLAR / JAPANESE YEN",146.812,146.836,0.2145]}
      ]
    })";
    std::unordered_map<std::string, fxprice::Quote> quotes;
    std::string error;
    Check(fxprice::ParseScannerResponse(response, quotes, error),
          "解析 TradingView Scanner 响应");
    Check(quotes.size() == 2, "解析全部外汇报价");
    Check(std::abs(quotes["EURUSD"].bid - 1.15831) < 0.0000001,
          "读取买一价");
    Check(std::abs(quotes["EURUSD"].ask - 1.15857) < 0.0000001,
          "读取卖一价");
    Check(std::abs(quotes["EURUSD"].midpoint - 1.15844) < 0.0000001,
          "计算中间价");
    Check(quotes["EURUSD"].precision == 5, "保留报价精度");
    Check(quotes["EURUSD"].dailyChangePercent &&
              std::abs(*quotes["EURUSD"].dailyChangePercent + 0.5836408892) < 0.0000001,
          "读取日涨跌幅");
    Check(fxprice::FormatQuoteValue(quotes["EURUSD"]) ==
              L"买 1.15831 卖 1.15857 中 1.15844 -0.58%",
          "格式化完整买卖行情");
    Check(fxprice::FormatQuoteSummary(quotes["EURUSD"]) == L"1.15844 -0.58%",
          "格式化汇总行情");
    Check(fxprice::FormatQuoteDetail(quotes["EURUSD"]).find(L"EURO / U.S. DOLLAR") !=
              std::wstring::npos,
          "详情包含标的描述");
    Check(!fxprice::ParseScannerResponse(
              R"({"totalCount":1,"data":[{"s":"FX_IDC:EURUSD","d":["EURUSD","EUR/USD",null,null,null]}]})",
              quotes, error),
          "拒绝缺少买卖价的响应");

    FakeTransport transport;
    transport.responseBody = response;
    fxprice::ScannerClient client(transport);
    quotes.clear();
    Check(client.Fetch({L"EURUSD", L"USDJPY"}, quotes, error),
          "通过注入传输层获取行情");
    Check(transport.lastHost == L"scanner.tradingview.com" &&
              transport.lastPath == L"/forex/scan",
          "请求 TradingView Forex Scanner 地址");
    Check(transport.lastBody.find("FX_IDC:EURUSD") != std::string::npos,
          "客户端发送外汇 ticker");

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
    Check(config.Save(settings), "保存无凭据插件配置");
    const fxprice::Settings loaded = config.Load();
    Check(loaded.refreshSeconds == 17, "读取刷新间隔");
    Check(loaded.symbols == settings.symbols, "读取全部外汇标的");
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
