#include "BiquoteClient.h"
#include "Module.h"
#include "OptionsWindow.h"

#include <Windows.h>

#include <atomic>
#include <iostream>
#include <string>

HINSTANCE g_moduleInstance{};

namespace
{
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
constexpr int kSymbolGroup = 1014;
constexpr int kGlobalGroup = 1015;
constexpr int kPrecisionHint = 1016;
int failures{};

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void CheckLayout(HWND window)
{
    RECT symbolGroup{}, globalGroup{};
    Check(GetWindowRect(GetDlgItem(window, kSymbolGroup), &symbolGroup) != FALSE,
          "存在独立配置分组");
    Check(GetWindowRect(GetDlgItem(window, kGlobalGroup), &globalGroup) != FALSE,
          "存在全局配置分组");
    Check(symbolGroup.bottom < globalGroup.top, "独立与全局配置分组互不重叠");
    RECT client{};
    GetClientRect(window, &client);
    Check(client.right == 660 && client.bottom == 480,
          "窗口尺寸与 Crypto 设置页一致，保持紧凑布局");
    RECT symbol{}, precision{}, add{}, update{}, remove{};
    GetWindowRect(GetDlgItem(window, kSymbolEdit), &symbol);
    GetWindowRect(GetDlgItem(window, kPrecisionEdit), &precision);
    GetWindowRect(GetDlgItem(window, kAddButton), &add);
    GetWindowRect(GetDlgItem(window, kUpdateButton), &update);
    GetWindowRect(GetDlgItem(window, kRemoveButton), &remove);
    Check(add.left > symbol.right && add.left > precision.right &&
              add.left == update.left && update.left == remove.left &&
              add.bottom < update.top && update.bottom < remove.top,
          "参照 Crypto 设置页，标的操作按钮在编辑区右侧竖排");
    for (const int id : {kSymbolList, kSymbolEdit, kPrecisionEdit, kPrecisionHint,
                         kAddButton, kUpdateButton, kRemoveButton})
    {
        RECT control{};
        GetWindowRect(GetDlgItem(window, id), &control);
        Check(control.left > symbolGroup.left && control.right < symbolGroup.right &&
                  control.top > symbolGroup.top && control.bottom < symbolGroup.bottom,
              "独立配置控件均在独立分组内");
    }
    for (const int id : {kRefreshEdit, kSourceCombo, kShowBid, kShowAsk, kShowMid, kShowChange})
    {
        RECT control{};
        GetWindowRect(GetDlgItem(window, id), &control);
        Check(control.left > globalGroup.left && control.right < globalGroup.right &&
                  control.top > globalGroup.top && control.bottom < globalGroup.bottom,
              "全局配置控件均在全局分组内");
    }

    const HWND hint = GetDlgItem(window, kPrecisionHint);
    wchar_t text[64]{};
    const int length = GetWindowTextW(hint, text, 64);
    Check(std::wstring(text) == L"留空自动", "小数位提示简洁");
    const HDC dc = GetDC(hint);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(hint, WM_GETFONT, 0, 0));
    const HGDIOBJ previous = SelectObject(dc, font);
    SIZE extent{};
    GetTextExtentPoint32W(dc, text, length, &extent);
    SelectObject(dc, previous);
    ReleaseDC(hint, dc);
    RECT hintRect{};
    GetClientRect(hint, &hintRect);
    Check(extent.cx <= hintRect.right && extent.cy <= hintRect.bottom,
          "小数位提示可完整显示");
}

class FakeTransport final : public fxprice::IHttpTransport
{
public:
    bool Get(const std::wstring&, const std::wstring&, std::string& response,
             std::string& error) override
    {
        ++requests;
        Sleep(30); // 结果交回 UI 前，允许测试检查等待状态和取消操作。
        response = body;
        error = succeeds ? "" : "模拟网络错误";
        return succeeds;
    }

    std::string body;
    bool succeeds{true};
    std::atomic<int> requests{};
};

struct Scenario
{
    const wchar_t* symbol;
    int command;
    bool valid;
    bool cancel{};
    bool needsValidation{true};
    int stage{};
    ULONGLONG started{GetTickCount64()};
};
Scenario* active{};

void CALLBACK DriveWindow(HWND, UINT, UINT_PTR, DWORD)
{
    HWND window{};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM context) -> BOOL {
        wchar_t className[64]{};
        GetClassNameW(candidate, className, 64);
        if (std::wstring(className) != L"TrafficMonitorFxPriceOptions") return TRUE;
        *reinterpret_cast<HWND*>(context) = candidate;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&window));
    if (!window) return;
    Scenario& scenario = *active;
    if (GetTickCount64() - scenario.started > 5000)
    {
        Check(false, "选项窗口校验超时");
        SendMessageW(window, WM_CLOSE, 0, 0);
        return;
    }
    if (scenario.stage == 0)
    {
        scenario.stage = 1;
        CheckLayout(window);
        SetWindowTextW(GetDlgItem(window, kRefreshEdit), L"31");
        SendDlgItemMessageW(window, kShowBid, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(GetDlgItem(window, kSymbolEdit), scenario.symbol);
        SetWindowTextW(GetDlgItem(window, kPrecisionEdit), L"3");
        SendMessageW(window, WM_COMMAND, scenario.command, 0);
        if (scenario.needsValidation)
        {
            Check(!IsWindowEnabled(GetDlgItem(window, IDOK)),
                  "查询完成前禁止保存，避免未校验标的进入配置");
            Check(SendDlgItemMessageW(window, kSymbolList, LB_GETCOUNT, 0, 0) == 1,
                  "查询完成前不能修改列表");
            // 重复点击（包括直接发送命令）不能启动第二次请求或绕过校验。
            SendMessageW(window, WM_COMMAND, kAddButton, 0);
            SendMessageW(window, WM_COMMAND, IDOK, 0);
            if (scenario.cancel)
                SendMessageW(window, WM_CLOSE, 0, 0);
        }
        return;
    }
    if (!IsWindowEnabled(GetDlgItem(window, IDOK))) return;

    Check(GetDlgItemInt(window, kRefreshEdit, nullptr, FALSE) == 31 &&
              SendDlgItemMessageW(window, kShowBid, BM_GETCHECK, 0, 0) == BST_UNCHECKED,
          "标的操作不重置全局配置");
    const int expectedCount = scenario.valid && scenario.command == kAddButton ? 2 : 1;
    Check(SendDlgItemMessageW(window, kSymbolList, LB_GETCOUNT, 0, 0) == expectedCount,
          "仅校验成功才能添加或更新列表");
    if (!scenario.valid)
    {
        wchar_t status[512]{};
        GetDlgItemTextW(window, kStatusText, status, 512);
        Check(std::wstring(status).find(L"校验失败") != std::wstring::npos,
              "失败时显示明确错误信息");
        wchar_t caption[128]{};
        SendDlgItemMessageW(window, kSymbolList, LB_GETTEXT, 0,
                            reinterpret_cast<LPARAM>(caption));
        Check(std::wstring(caption).find(L"EURUSD.s") != std::wstring::npos,
              "校验失败后保留原有标的");
    }
    SendMessageW(window, WM_COMMAND, scenario.valid ? IDOK : IDCANCEL, 0);
}

void Run(Scenario scenario, const std::string& response, bool transportSucceeds = true)
{
    FakeTransport transport;
    transport.body = response;
    transport.succeeds = transportSucceeds;
    fxprice::Settings settings;
    settings.symbols = {fxprice::SymbolConfig{L"EURUSD", -1}};
    settings.refreshSeconds = 15;
    settings.display.showBid = true;
    active = &scenario;
    const UINT_PTR timer = SetTimer(nullptr, 0, 10, DriveWindow);
    Check(timer != 0, "创建测试定时器");
    if (!timer) return;
    const bool accepted = fxprice::OptionsWindow::Show(nullptr, settings, &transport);
    KillTimer(nullptr, timer);
    active = nullptr;
    Check(accepted == (scenario.valid && !scenario.cancel), "校验结果决定能否保存");
    Check(transport.requests == (scenario.needsValidation ? 1 : 0),
          "每次变更只查询一次，修改精度无需联网");
    if (accepted)
    {
        const bool adding = scenario.command == kAddButton;
        Check(settings.refreshSeconds == 31 && !settings.display.showBid,
              "全局配置随确定保存");
        Check(settings.symbols.size() == (adding ? 2u : 1u), "保存后的标的数量正确");
        if (adding)
            Check(settings.symbols[0].precision == -1, "新标的小数位不影响已有标的");
        const auto& symbol = settings.symbols[adding ? 1 : 0];
        Check(symbol.code == fxprice::NormalizeSymbol(scenario.symbol) &&
                  symbol.precision == 3,
              "保存通过校验的标的及其精度");
    }
    else
    {
        Check(settings.refreshSeconds == 15 && settings.display.showBid,
              "失败或取消不改变全局配置");
        Check(settings.symbols.size() == 1 && settings.symbols[0].code == L"EURUSD" &&
                  settings.symbols[0].precision == -1,
              "失败或取消不能改变调用方配置");
    }
}
} // namespace

int main()
{
    g_moduleInstance = GetModuleHandleW(nullptr);
    const std::string valid = R"({"GBPUSD":{"bid":1.25,"ask":1.26}})";
    Run({L"gbp/usd.s", kAddButton, true}, valid);
    Run({L"ABCDEF", kAddButton, false}, "{}");
    Run({L"GBPUSD", kAddButton, false}, R"({"USDJPY":{"bid":150,"ask":151}})");
    Run({L"GBPUSD", kAddButton, false}, R"({"GBPUSD":{"bid":0,"ask":0}})");
    Run({L"GBPUSD", kAddButton, false}, valid, false);
    Run({L"GBPUSD", kUpdateButton, true}, valid);
    Run({L"ABCDEF", kUpdateButton, false}, "{}");
    Run({L"GBPUSD", IDOK, true}, valid);
    Run({L"ABCDEF", IDOK, false}, "{}");
    Run({L"EURUSD", kUpdateButton, true, false, false}, "{}", false);
    Run({L"GBPUSD", kAddButton, true, true}, valid);
    if (failures == 0) std::cout << "All options tests passed\n";
    return failures == 0 ? 0 : 1;
}
