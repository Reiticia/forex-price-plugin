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
constexpr int kStatusText = 1008;
constexpr int kPrecisionEdit = 1013;
int failures{};

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
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
        Check(settings.symbols.size() == (adding ? 2u : 1u), "保存后的标的数量正确");
        const auto& symbol = settings.symbols[adding ? 1 : 0];
        Check(symbol.code == fxprice::NormalizeSymbol(scenario.symbol) &&
                  symbol.precision == 3,
              "保存通过校验的标的及其精度");
    }
    else
    {
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
