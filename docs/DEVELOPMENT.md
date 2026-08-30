# 开发文档

## 1. 项目概述

FxPrice 是面向 TrafficMonitor 的 Windows C++ 外汇行情插件，当前版本为 0.4.0。它通过 biquote.io 公共 API 读取外汇报价，不需要注册、登录、账户 ID 或 API Key。

核心约束：

- 配置 1–8 个外汇交易对；
- 数据源固定为 biquote.io，不提供交易所切换；
- “显示内容”为全局选项：任务栏可独立勾选买一价、卖一价、中间价，工具提示可勾选日涨跌幅；
- 小数位数按交易对独立设置（`SymbolConfig.precision`）：-1 自动推断，0–10 固定；
- 网络请求必须在后台线程执行；
- 保留旧显示项 ID 和旧 INI 读取能力。

## 2. 开发环境

- Windows 10 或更高版本；
- Visual Studio 2022 C++ 桌面开发工作负载；
- Windows 10/11 SDK；
- CMake 3.20 或更高版本；
- C++17。

运行时只依赖 WinHTTP 和 User32，不使用 MFC 或第三方 JSON 库（响应解析为项目内自研 JSON 解析器）。

## 3. 目录结构

| 路径 | 职责 |
| --- | --- |
| `src/PluginInterface.h` | TrafficMonitor API v8 ABI |
| `src/FxPricePlugin.*` | 插件入口、显示项、后台调度和格式化 |
| `src/BiquoteClient.*` | biquote.io 的 WinHTTP 客户端与传输抽象 |
| `src/MarketData.*` | `/api/latest` 查询串构造与响应解析 |
| `src/Quote.*` | 报价结构、符号规范化、精度推断与显示格式化 |
| `src/Config.*` | 交易对、显示选项、小数位数及 INI 持久化 |
| `src/OptionsWindow.*` | 纯 Win32 设置窗口 |
| `tests/Tests.cpp` | 解析、格式化、配置单元测试 |
| `tests/PluginSmoke.cpp` | DLL ABI 冒烟测试 |
| `tests/LiveSmoke.cpp` | 真实 biquote 请求测试 |
| `tests/PluginLiveSmoke.cpp` | 加载 DLL 端到端刷新测试 |
| `tests/OptionsPreview.cpp` | 打开 Win32 配置窗口的人工验收入口 |

## 4. 运行流程

```text
TrafficMonitor::DataRequired
        │
        ├─ 应用上一轮 pending 结果
        └─ 到达刷新时间后复制当前 Settings
                    │
                    └─ 后台线程（模块固定防卸载）
                              │
                              └─ BiquoteClient::Fetch
                                  ├─ BuildLatestQuery → /api/latest?symbols=...
                                  └─ ParseLatestResponse → Quote 缓存
                                        │
                                        └─ 回传 pending_，UI 线程应用
```

设置变化时 generation 递增。旧设置启动的迟到响应会被丢弃，避免切换交易对后短暂显示旧数据。

## 5. 统一数据模型

### SymbolConfig

```cpp
struct SymbolConfig
{
    std::wstring code{L"EURUSD"};
    int precision{-1};   // -1 自动推断，0..10 固定
};
```

- 内部代码统一为 6 位大写字母（如 `EURUSD`）；
- 展示时显示为 `EURUSD.s`；
- `precision` 为该交易对独立的小数位数，自动模式按行情价格文本推断。

### DisplayOptions

```cpp
struct DisplayOptions
{
    bool showBid{true};
    bool showAsk{true};
    bool showMid{true};
    bool showChange{true};
};
```

全局显示开关，作用于所有交易对；买一价/卖一价/中间价用于任务栏和工具提示，日涨跌幅仅用于工具提示；不包含小数位数（后者按交易对独立）。

### Quote

统一字段：

- `symbol`、`description`
- `bid`、`ask`、`midpoint`（优先服务端 `mid`，缺失按 `(bid+ask)/2` 计算）
- `dailyChangePercent`（服务端 `dayDiffPercent`，相对前收的日涨跌，非严格滚动 24 小时）
- `precision`（解析时按 bid/ask 文本小数位推断的自动精度）

## 6. 数据源适配

### biquote.io

```http
GET https://biquote.io/api/latest?symbols=EURUSD&symbols=GBPUSD
```

- 参数为重复的 `symbols`，不支持逗号分隔；
- 响应为以标的代码为键的 tick 对象字典；
- 无效或不存在的交易对会被服务端静默跳过（对应显示项显示 `--`）；
- 外汇休市时返回 200 与最后一笔报价，`marketState` 为 `closed`，插件照常显示；
- 匿名限流 15000 次/分钟/IP，远高于插件刷新频率。

## 7. 显示项规则

- `GetItem(0)` 是汇总项，ID 为 `FxPriceSummary`。
- `GetItem(1..N)` 是各交易对独立项，ID 为 `FxPriceItem0..7`。
- `GetItemValueText()` 只返回缓存，不能访问网络。
- 任务栏标的名显示为 `EURUSD.s`；独立项采用自绘以绕过 TrafficMonitor 按 ItemId 缓存旧标签的问题。
- 任务栏格式化只拼装勾选的价格字段：`买:1.15823 卖:1.15829 中:1.15826`；日涨跌幅只在工具提示中显示。

## 8. 配置文件

新配置文件为 TrafficMonitor 插件配置目录下的 `FxPrice.ini`：

```ini
[General]
RefreshSeconds=10
ShowBid=1
ShowAsk=1
ShowMid=1
ShowChange=1
Count=2

[Symbols]
Symbol0=EURUSD
Precision0=5
Symbol1=GBPUSD
Precision1=-1
```

规则：

- `RefreshSeconds` 范围为 2–300；
- `ShowBid/ShowAsk/ShowMid/ShowChange` 为全局显示开关（0/1），其中 `ShowChange` 只影响工具提示；
- `SymbolN` 为该交易对代码，`PrecisionN` 为该交易对的小数位数（-1 自动，0–10 固定）；
- 新配置缺少 `PrecisionN` 时兼容读取旧版 `General.DisplayPrecision`；两者都缺失或精度值非法时回退 -1（自动推断）；
- `Count` 范围为 1–8。

## 9. 构建

默认构建链路为 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe`。以下命令通过
Visual Studio 生成器创建工程，再由 MSBuild 调用 MSVC 编译器；当前不使用 Clang。

x64：

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

Win32：

```powershell
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
ctest --test-dir build-win32 -C Release --output-on-failure
```

产物为 `FxPricePlugin.dll`。插件 DLL 与 TrafficMonitor 必须使用相同架构。

`FxPriceLiveSmoke.exe` 和 `FxPricePluginLiveSmoke.exe` 需要联网，默认不注册到 CTest，
避免网络波动影响离线构建。

## 10. 修改检查清单

- 不改变官方 API v8 虚表顺序；
- 不在显示项 getter 或绘制回调中请求网络；
- 新增字段解析需更新离线样本与测试；
- 小数位数新增/修改时同时覆盖自动与固定（0–10）路径；
- 交易对增删改后必须保持 `SymbolN` 与 `PrecisionN` 的索引对齐；
- 切换配置后必须丢弃旧 generation 响应；
- 发布版本号必须同步更新 CMake 项目版本、`TMI_VERSION`、WinHTTP User-Agent 与 `CHANGELOG.md`；
- 新接口补充离线样本、真实行情测试和部署域名。

## 11. 参考资料

- [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97)
- [biquote.io API 文档](https://biquote.io/docs/)
- [crypto-price-plugin](https://github.com/Reiticia/crypto-price-plugin)
