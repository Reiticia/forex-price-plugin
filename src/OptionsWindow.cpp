#include "OptionsWindow.h"

#include "Module.h"
#include "Quote.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>

namespace fxprice
{
namespace
{
constexpr wchar_t kWindowClass[] = L"TrafficMonitorFxPriceOptions";
constexpr int kSymbolList = 1001;
constexpr int kSymbolEdit = 1002;
constexpr int kAddButton = 1003;
constexpr int kUpdateButton = 1004;
constexpr int kRemoveButton = 1005;
constexpr int kRefreshEdit = 1006;
constexpr int kSourceCombo = 1007;
constexpr int kStatusText = 1008;
constexpr int kShowBid = 1009;
constexpr int kShowAsk = 1010;
constexpr int kShowMid = 1011;
constexpr int kShowChange = 1012;
constexpr int kPrecisionEdit = 1013;

HWND AddControl(HWND parent, const wchar_t* className, const wchar_t* text,
                DWORD style, int x, int y, int width, int height, int id)
{
    HWND control = CreateWindowExW(
        0, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_moduleInstance, nullptr);
    if (control)
    {
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    return control;
}
} // namespace

bool OptionsWindow::Show(HWND parent, Settings& settings)
{
    OptionsWindow dialog(settings);
    if (!dialog.Create(parent))
    {
        MessageBoxW(parent, L"无法创建选项窗口。", L"外汇行情", MB_OK | MB_ICONERROR);
        return false;
    }

    const bool parentWasEnabled = parent && IsWindowEnabled(parent);
    if (parentWasEnabled) EnableWindow(parent, FALSE);
    ShowWindow(dialog.window_, SW_SHOW);
    UpdateWindow(dialog.window_);

    MSG message{};
    while (IsWindow(dialog.window_))
    {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(dialog.window_, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (parentWasEnabled)
    {
        EnableWindow(parent, TRUE);
        SetActiveWindow(parent);
    }
    if (dialog.accepted_) settings = std::move(dialog.draft_);
    return dialog.accepted_;
}

bool OptionsWindow::Create(HWND parent)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = g_moduleInstance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    RECT rectangle{0, 0, 660, 480};
    AdjustWindowRectEx(&rectangle, WS_CAPTION | WS_SYSMENU, FALSE,
                       WS_EX_DLGMODALFRAME);
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (parent)
    {
        RECT parentRect{};
        if (GetWindowRect(parent, &parentRect))
        {
            x = parentRect.left +
                ((parentRect.right - parentRect.left) -
                 (rectangle.right - rectangle.left)) / 2;
            y = parentRect.top +
                ((parentRect.bottom - parentRect.top) -
                 (rectangle.bottom - rectangle.top)) / 2;
        }
    }

    window_ = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kWindowClass, L"外汇行情设置",
        WS_CAPTION | WS_SYSMENU, x, y,
        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        parent, nullptr, g_moduleInstance, this);
    return window_ != nullptr;
}

LRESULT CALLBACK OptionsWindow::WindowProcedure(HWND window, UINT message,
                                                WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<OptionsWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OptionsWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT OptionsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateControls();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case kSymbolList:
            if (HIWORD(wParam) == LBN_SELCHANGE) LoadSelection();
            return 0;
        case kAddButton:
            AddNew();
            return 0;
        case kUpdateButton:
            UpdateSelected();
            return 0;
        case kRemoveButton:
            RemoveSelected();
            return 0;
        case IDOK:
            Accept();
            return 0;
        case IDCANCEL:
            DestroyWindow(window_);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        window_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void OptionsWindow::CreateControls()
{
    AddControl(window_, L"STATIC", L"已监控标的（最多 8 个）",
               0, 18, 14, 260, 20, -1);
    list_ = AddControl(window_, L"LISTBOX", L"",
                       WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
                       18, 36, 624, 186, kSymbolList);

    // 左列中文标签宽度统一为 72，避免"交易对/刷新间隔"被输入框遮挡。
    constexpr int kLabelWidth = 72;
    constexpr int kLabelY = 234;
    constexpr int kControlY = 232;

    AddControl(window_, L"STATIC", L"交易对", 0, 18, kLabelY, kLabelWidth, 20, -1);
    symbol_ = AddControl(window_, L"EDIT", L"",
                         WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                         94, kControlY, 148, 25, kSymbolEdit);
    SendMessageW(symbol_, EM_SETLIMITTEXT, 16, 0);

    AddControl(window_, L"STATIC", L"数据源", 0, 258, kLabelY, 56, 20, -1);
    const HWND source = AddControl(window_, L"COMBOBOX", L"",
                                   WS_TABSTOP | CBS_DROPDOWNLIST,
                                   318, kControlY, 180, 120, kSourceCombo);
    SendMessageW(source, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"biquote.io"));
    SendMessageW(source, CB_SETCURSEL, 0, 0);
    EnableWindow(source, FALSE);

    // "小数位数"作用于当前选中交易对：留空自动推断，填 0–10 固定显示位数。
    AddControl(window_, L"STATIC", L"小数位数", 0, 18, 268, kLabelWidth, 20, -1);
    precision_ = AddControl(window_, L"EDIT", L"",
                            WS_BORDER | WS_TABSTOP | ES_NUMBER,
                            94, 264, 44, 25, kPrecisionEdit);
    SendMessageW(precision_, EM_SETLIMITTEXT, 4, 0);
    AddControl(window_, L"STATIC", L"留空自动；0–10", 0, 148, 268, 100, 20, -1);

    AddControl(window_, L"STATIC", L"刷新间隔", 0, 258, 268, 72, 20, -1);
    refresh_ = AddControl(window_, L"EDIT",
                          std::to_wstring(draft_.refreshSeconds).c_str(),
                          WS_BORDER | WS_TABSTOP | ES_NUMBER,
                          334, 264, 70, 25, kRefreshEdit);
    AddControl(window_, L"STATIC", L"秒（2–300）", 0, 410, 268, 100, 20, -1);

    // 任务栏仅显示价格字段；日涨跌幅只用于工具提示。
    AddControl(window_, L"STATIC", L"任务栏内容", 0, 18, 302, kLabelWidth, 20, -1);
    showBid_ = AddControl(window_, L"BUTTON", L"买一价",
                          WS_TABSTOP | BS_AUTOCHECKBOX,
                          94, 298, 78, 22, kShowBid);
    showAsk_ = AddControl(window_, L"BUTTON", L"卖一价",
                          WS_TABSTOP | BS_AUTOCHECKBOX,
                          178, 298, 78, 22, kShowAsk);
    showMid_ = AddControl(window_, L"BUTTON", L"中间价",
                          WS_TABSTOP | BS_AUTOCHECKBOX,
                          262, 298, 78, 22, kShowMid);
    showChange_ = AddControl(window_, L"BUTTON", L"提示日涨跌",
                             WS_TABSTOP | BS_AUTOCHECKBOX,
                             346, 298, 104, 22, kShowChange);
    SendMessageW(showBid_, BM_SETCHECK, draft_.display.showBid ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(showAsk_, BM_SETCHECK, draft_.display.showAsk ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(showMid_, BM_SETCHECK, draft_.display.showMid ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(showChange_, BM_SETCHECK, draft_.display.showChange ? BST_CHECKED : BST_UNCHECKED, 0);

    AddControl(window_, L"STATIC", L"涨跌口径", 0, 18, 332, kLabelWidth, 20, -1);
    AddControl(window_, L"STATIC", L"相对前收，非严格滚动 24 小时",
               0, 94, 332, 400, 20, -1);

    AddControl(window_, L"BUTTON", L"添加新标的",
               WS_TABSTOP | BS_PUSHBUTTON, 530, 232, 112, 27, kAddButton);
    AddControl(window_, L"BUTTON", L"更新选中项",
               WS_TABSTOP | BS_PUSHBUTTON, 530, 266, 112, 27, kUpdateButton);
    AddControl(window_, L"BUTTON", L"删除选中项",
               WS_TABSTOP | BS_PUSHBUTTON, 530, 300, 112, 27, kRemoveButton);

    status_ = AddControl(
        window_, L"STATIC",
        L"输入如 EURUSD.s、EUR/USD；会自动规范化。",
        0, 18, 392, 610, 22, kStatusText);
    AddControl(window_, L"BUTTON", L"确定",
               WS_TABSTOP | BS_DEFPUSHBUTTON, 454, 434, 88, 29, IDOK);
    AddControl(window_, L"BUTTON", L"取消",
               WS_TABSTOP | BS_PUSHBUTTON, 554, 434, 88, 29, IDCANCEL);

    RefreshList(draft_.symbols.empty() ? -1 : 0);
    SetFocus(list_);
}

void OptionsWindow::RefreshList(int selection)
{
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const SymbolConfig& symbol : draft_.symbols)
    {
        const std::wstring caption = DisplaySymbol(symbol.code) +
                                     L"    biquote.io";
        SendMessageW(list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(caption.c_str()));
    }
    if (selection >= 0 && selection < static_cast<int>(draft_.symbols.size()))
    {
        SendMessageW(list_, LB_SETCURSEL, selection, 0);
        LoadSelection();
    }
}

int OptionsWindow::SelectedIndex() const
{
    return static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
}

void OptionsWindow::LoadSelection()
{
    const int index = SelectedIndex();
    if (index < 0 || index >= static_cast<int>(draft_.symbols.size())) return;
    const SymbolConfig& symbol = draft_.symbols[static_cast<std::size_t>(index)];
    SetWindowTextW(symbol_, DisplaySymbol(symbol.code).c_str());
    if (symbol.precision >= 0)
        SetWindowTextW(precision_, std::to_wstring(symbol.precision).c_str());
    else
        SetWindowTextW(precision_, L"");
    SetStatus(L"修改后可点击“更新选中项”，点击“确定”也会保存当前编辑。");
}

int OptionsWindow::ReadPrecisionInput() const
{
    std::array<wchar_t, 8> precisionText{};
    GetWindowTextW(precision_, precisionText.data(),
                   static_cast<int>(precisionText.size()));
    if (precisionText[0] == L'\0') return -1; // 留空 = 自动推断
    wchar_t* end = nullptr;
    const long value = std::wcstol(precisionText.data(), &end, 10);
    if (end == precisionText.data() || *end != L'\0' || value < 0 || value > 10)
        return -2; // 非法输入
    return static_cast<int>(value);
}

SymbolConfig OptionsWindow::ReadSymbolConfig() const
{
    std::array<wchar_t, 32> value{};
    GetWindowTextW(symbol_, value.data(), static_cast<int>(value.size()));
    return {NormalizeSymbol(value.data()), ReadPrecisionInput()};
}

void OptionsWindow::AddNew()
{
    const SymbolConfig symbol = ReadSymbolConfig();
    if (!IsValidSymbol(symbol.code))
    {
        SetStatus(L"交易对格式无效：请输入两个三位货币代码，例如 EURUSD.s 或 EUR/USD。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (symbol.precision == -2)
    {
        SetStatus(L"小数位数必须是 0 到 10 之间的整数，或留空为自动判断。 ");
        SetFocus(precision_);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (std::find_if(draft_.symbols.begin(), draft_.symbols.end(),
                     [&](const SymbolConfig& existing) {
                         return existing.code == symbol.code;
                     }) != draft_.symbols.end())
    {
        SetStatus(L"该交易对已经在监控列表中。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (draft_.symbols.size() >= kMaxSymbols)
    {
        SetStatus(L"最多只能同时监控 8 个交易对。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    draft_.symbols.push_back(symbol);
    RefreshList(static_cast<int>(draft_.symbols.size() - 1));
    SetStatus(L"新交易对已添加；点击“确定”后保存并立即刷新。 ");
}

void OptionsWindow::UpdateSelected()
{
    const int selected = SelectedIndex();
    if (selected < 0 || selected >= static_cast<int>(draft_.symbols.size()))
    {
        SetStatus(L"请先在列表中选择要更新的交易对。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    const SymbolConfig symbol = ReadSymbolConfig();
    if (!IsValidSymbol(symbol.code))
    {
        SetStatus(L"交易对格式无效：请输入两个三位货币代码，例如 EURUSD.s 或 EUR/USD。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (symbol.precision == -2)
    {
        SetStatus(L"小数位数必须是 0 到 10 之间的整数，或留空为自动判断。 ");
        SetFocus(precision_);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    const auto duplicate = std::find_if(draft_.symbols.begin(), draft_.symbols.end(),
                                        [&](const SymbolConfig& existing) {
                                            return existing.code == symbol.code;
                                        });
    if (duplicate != draft_.symbols.end() &&
        duplicate != draft_.symbols.begin() + selected)
    {
        SetStatus(L"该交易对已经在监控列表中。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    draft_.symbols[static_cast<std::size_t>(selected)] = symbol;
    RefreshList(selected);
    SetStatus(L"列表已更新；点击“确定”后保存并立即刷新。 ");
}

void OptionsWindow::RemoveSelected()
{
    const int selected = SelectedIndex();
    if (selected < 0 || selected >= static_cast<int>(draft_.symbols.size()))
    {
        SetStatus(L"请先在列表中选择要删除的交易对。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    draft_.symbols.erase(draft_.symbols.begin() + selected);
    const int next = draft_.symbols.empty()
        ? -1 : (std::min)(selected, static_cast<int>(draft_.symbols.size() - 1));
    RefreshList(next);
    if (next < 0)
    {
        SetWindowTextW(symbol_, L"");
        SetWindowTextW(precision_, L"");
    }
    SetStatus(L"已从列表移除；点击“确定”后生效。 ");
}

void OptionsWindow::Accept()
{
    if (draft_.symbols.empty())
    {
        SetStatus(L"请至少添加一个外汇交易对。 ");
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const int selected = SelectedIndex();
    if (selected >= 0 && selected < static_cast<int>(draft_.symbols.size()))
    {
        const SymbolConfig symbol = ReadSymbolConfig();
        if (!IsValidSymbol(symbol.code))
        {
            SetStatus(L"交易对格式无效：请输入两个三位货币代码，例如 EURUSD.s 或 EUR/USD。 ");
            MessageBeep(MB_ICONWARNING);
            return;
        }
        if (symbol.precision == -2)
        {
            SetStatus(L"小数位数必须是 0 到 10 之间的整数，或留空为自动判断。 ");
            SetFocus(precision_);
            MessageBeep(MB_ICONWARNING);
            return;
        }
        const auto duplicate = std::find_if(draft_.symbols.begin(), draft_.symbols.end(),
                                            [&](const SymbolConfig& existing) {
                                                return existing.code == symbol.code;
                                            });
        if (duplicate != draft_.symbols.end() &&
            duplicate != draft_.symbols.begin() + selected)
        {
            SetStatus(L"该交易对已经在监控列表中。 ");
            MessageBeep(MB_ICONWARNING);
            return;
        }
        draft_.symbols[static_cast<std::size_t>(selected)] = symbol;
    }

    std::array<wchar_t, 16> value{};
    GetWindowTextW(refresh_, value.data(), static_cast<int>(value.size()));
    wchar_t* end = nullptr;
    const long seconds = std::wcstol(value.data(), &end, 10);
    if (end == value.data() || *end != L'\0' || seconds < 2 || seconds > 300)
    {
        SetStatus(L"刷新间隔必须是 2 到 300 秒之间的整数。 ");
        SetFocus(refresh_);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    draft_.refreshSeconds = static_cast<int>(seconds);

    draft_.display.showBid =
        SendMessageW(showBid_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    draft_.display.showAsk =
        SendMessageW(showAsk_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    draft_.display.showMid =
        SendMessageW(showMid_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    draft_.display.showChange =
        SendMessageW(showChange_, BM_GETCHECK, 0, 0) == BST_CHECKED;

    accepted_ = true;
    DestroyWindow(window_);
}

void OptionsWindow::SetStatus(const wchar_t* text)
{
    SetWindowTextW(status_, text);
}
} // namespace fxprice
