#include "BiquoteClient.h"
#include "Quote.h"

#include <iostream>
#include <unordered_map>

int main(int argc, char** argv)
{
    fxprice::QuoteSource source = fxprice::QuoteSource::Biquote;
    if (argc > 1)
    {
        const std::string name = argv[1];
        if (name == "bitget-ecn") source = fxprice::QuoteSource::BitgetEcn;
        else if (name == "bitget-standard") source = fxprice::QuoteSource::BitgetStandard;
        else if (name == "bitget-pro") source = fxprice::QuoteSource::BitgetPro;
        else { std::cerr << "Unknown source\n"; return 3; }
    }
    fxprice::WinHttpTransport transport;
    fxprice::MarketClient client(transport, source);
    std::unordered_map<std::string, fxprice::Quote> quotes;
    std::string error;
    if (!client.Fetch({L"EURUSD", L"USDJPY"}, quotes, error))
    {
        std::cerr << "Live request failed: " << error << '\n';
        return 1;
    }
    const auto found = quotes.find("EURUSD");
    if (found == quotes.end())
    {
        std::cerr << "Live response did not contain EURUSD\n";
        return 2;
    }
    const fxprice::Quote& quote = found->second;
    std::cout << quote.symbol << " bid=" << quote.bid << " ask=" << quote.ask
              << " mid=" << quote.midpoint << " daily_change=";
    if (quote.dailyChangePercent) std::cout << *quote.dailyChangePercent << '%';
    else std::cout << "--";
    std::cout << '\n';
    return 0;
}
