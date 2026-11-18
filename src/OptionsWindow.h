#pragma once

#include "Config.h"

#include <Windows.h>

#include <future>
#include <utility>

namespace fxprice
{
class IHttpTransport;

class OptionsWindow
{
public:
    static bool Show(HWND parent, Settings& settings, IHttpTransport* transport = nullptr);

private:
    OptionsWindow(Settings settings, IHttpTransport& transport)
        : draft_(std::move(settings)), transport_(transport) {}

    enum class PendingAction { Add, Update, Accept, Source };
    void BeginValidation(const SymbolConfig& symbol, PendingAction action, int selection = -1);
    void CompleteValidation();
    void SetValidating(bool validating);
    void Close();

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                            WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool Create(HWND parent);
    void CreateControls();
    void RefreshList(int selection = -1);
    void LoadSelection();
    void AddNew();
    void UpdateSelected();
    void RemoveSelected();
    void Accept();
    SymbolConfig ReadSymbolConfig() const;
    int ReadPrecisionInput() const;
    int SelectedIndex() const;
    void SetStatus(const wchar_t* text);

    Settings draft_;
    IHttpTransport& transport_;
    std::future<std::string> validation_;
    SymbolConfig pendingSymbol_;
    PendingAction pendingAction_{PendingAction::Add};
    int pendingSelection_{-1};
    QuoteSource pendingSource_{QuoteSource::Biquote};
    bool closeRequested_{};
    HWND window_{};
    HWND list_{};
    HWND symbol_{};
    HWND refresh_{};
    HWND precision_{};
    HWND showBid_{};
    HWND showAsk_{};
    HWND showMid_{};
    HWND showChange_{};
    HWND status_{};
    bool accepted_{};
};
} // namespace fxprice
