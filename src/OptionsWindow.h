#pragma once

#include "Config.h"

#include <Windows.h>

#include <utility>

namespace fxprice
{
class OptionsWindow
{
public:
    static bool Show(HWND parent, Settings& settings);

private:
    explicit OptionsWindow(Settings settings) : draft_(std::move(settings)) {}

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
