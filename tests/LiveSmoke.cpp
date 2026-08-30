#include "BiquoteClient.h"
#include "Quote.h"

#include <iostream>
#include <unordered_map>

int main()
{
    fxprice::WinHttpTransport transport;
    fxprice::BiquoteClient client(transport);
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
