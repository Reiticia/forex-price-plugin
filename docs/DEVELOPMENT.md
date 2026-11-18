# 开发文档

## 1. 项目概述

FxPrice 是面向 TrafficMonitor 的 Windows C++ 外汇行情插件，当前版本为 0.4.0。它通过 biquote.io 或 Bitget CFD 读取外汇/贵金属报价；当前只发起不带账户或 API Key 的行情请求。

核心约束：

- 配置 1–8 个外汇交易对；
- 全局数据源支持 biquote、Bitget CFD ECN / .s / Pro；Bybit MT5 CFD 不支持 V5，暂不接入；
- “显示内容”为全局选项：任务栏可独立勾选买一价、卖一价、中间价和日涨跌幅，工具提示同步显示；
- 小数位数按交易对独立设置（`SymbolConfig.precision`）：-1 自动推断，0–10 固定；
- 网络请求必须在后台线程执行；
- 保留旧显示项 ID 和旧 INI 读取能力。

## 2. 开发环境

- Windows 10 或更高版本；
- Visual Studio 2022 C++ 桌面开发工作负载；
- Windows 10/11 SDK；
- CMake 3.21 或更高版本；
- C++17。

运行时只依赖 WinHTTP 和 User32，不使用 MFC 或第三方 JSON 库（响应解析为项目内自研 JSON 解析器）。

## 3. 目录结构

| 路径 | 职责 |
| --- | --- |
| `src/PluginInterface.h` | TrafficMonitor API v8 ABI |
| `src/FxPricePlugin.*` | 插件入口、显示项、后台调度和格式化 |
| `src/BiquoteClient.*` | WinHTTP 传输、BiquoteClient 及按来源路由的 MarketClient |
| `src/MarketData.*` | `/api/latest` 查询串构造与响应解析 |
| `src/MarketSchedule.h` | 固定 UTC+3 的周末暂停判断 |
| `src/Quote.*` | 报价结构、符号规范化、精度推断与显示格式化 |
| `src/Config.*` | 交易对、显示选项、小数位数及 INI 持久化 |
| `src/OptionsWindow.*` | 纯 Win32 设置窗口 |
| `tests/Tests.cpp` | 解析、格式化、配置单元测试 |
| `tests/PluginSmoke.cpp` | DLL ABI 冒烟测试 |
| `tests/OptionsTests.cpp` | 模拟行情传输的 Win32 配置窗口自动化测试 |
| `tests/LiveSmoke.cpp` | 真实 biquote / Bitget CFD 请求测试 |
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
                              └─ MarketClient::Fetch
                                  ├─ biquote: /api/latest → ParseLatestResponse
                                  └─ Bitget CFD: /api/v3/cfd/market/tickers → ParseBitgetCfdResponse
                                        │
                                        └─ 回传 pending_，宿主监控线程应用并发布不可变快照
```

TrafficMonitor 1.85.1 的 `DataRequired` 在监控线程调用，不是 UI 线程。
`stateMutex_` 串行保护设置、generation、调度、pending 和显示草稿；配置切换、generation 递增、
结果校验及应用处于同一同步边界，旧结果不得写入新配置。`configMutex_` 单独串行保护 INI 访问。

显示项及 tooltip 使用 `PublishedSnapshot` 发布不可变快照。每个线程/每个 getter 固定住最近快照，
返回的文本指针有效至同线程再次调用同一对象的同一 getter，或插件卸载；不同 getter 不会互相释放文本。
绘制和宽度测量一次读取同一快照。保留槽位由插件拥有，不使用可能在 DLL 卸载后执行析构的 TLS 对象；
内存按调用线程数/访问器数保留最近快照，不按报价次数累积。

后台刷新使用原生线程入口和普通 DLL 引用。工作函数正常返回、所有 C++ 局部对象及 WinHTTP 回调清理完成后，
外层入口才调用 `FreeLibraryAndExitThread`。`DllMain` 不调用单例、不加锁、不等待线程；静态析构无 join。
宿主释放其 DLL 引用时，进行中的工作保持模块存活直至完成/超时；无官方卸载回调，不能在宿主 FreeLibrary 时主动收到取消通知。
显式 `Shutdown`（非 DllMain）及配置切换可取消旧请求。

`StartRefresh` 在检查刷新间隔和启动后台线程之前，通过 `GetSystemTime` 读取 UTC：
UTC 21:00 起将星期顺延一天，以固定 UTC+3 判断周六、周日并直接返回，`force` 不能绕过。
暂停窗口为 UTC 周五 21:00（含）到周日 21:00（不含），不依赖电脑本地时区或夏令时。
暂停期间不发起行情刷新请求、不覆盖已显示的报价，
工具提示标记暂停状态；下一刷新时间被重置，因此周一首次回调即恢复抓取。进入周末前已发出的
请求允许完成；周末启动或修改配置后，没有报价缓存的项目保持 `--`。此策略不处理节假日
或纽约时区的开收盘时刻。

添加或替换标的的有效性校验独立于刷新调度：`OptionsWindow` 用 `std::async` 调用
`MarketClient::Fetch`，只在目标标的返回有效买卖报价后提交到草稿。切换数据源时验证全部已配置标的，失败恢复原来源；校验期间冻结来源选择。
窗口定时器在 UI 线程接收结果，校验期间禁止变更列表及保存；取消时立即置位请求取消，等异步请求及回调安全退出后关闭，
避免后台任务访问已销毁窗口或在 DLL 卸载后继续执行。仅修改精度不查询行情。
此用户主动校验在周末也可查询最后报价，但不写入刷新缓存。

每轮刷新/校验共用 `RequestControl`，默认总预算 20 秒（包括所有 Pro 子请求），每次独立 Fetch 创建新预算。
WinHTTP 异步等待每 25ms 检查取消/截止时间；终止时关闭请求、连接、会话，使用
`WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT` 确认最后回调已返回，随后才能销毁回调状态及释放 DLL 引用。
因此 20 秒是请求预算，不包含 Windows 完成安全回调收尾所需时间；不能为硬期限强行卸载仍在回调中的模块。

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
- 展示/请求后缀由 `Settings.source` 决定：biquote / Bitget Standard 为 `.s`，ECN 无后缀，Pro 为 `.pro`；
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

全局显示开关，作用于所有交易对；买一价/卖一价/中间价/日涨跌幅均用于任务栏和工具提示；不包含小数位数（后者按交易对独立）。

### Quote

统一字段：

- `symbol`、`description`
- `bid`、`ask`、`midpoint`（优先服务端 `mid`，缺失按 `(bid+ask)/2` 计算）
- `dailyChangePercent`（biquote 为 `dayDiffPercent` 相对前收；Bitget 为 `bidOpenPriceChange × 100` 相对买价日开盘；均非滚动 24 小时）
- `precision`（解析时按 bid/ask 文本小数位推断的自动精度）

## 6. 数据源适配

### biquote.io

```http
GET https://biquote.io/api/latest?symbols=EURUSD&symbols=GBPUSD
```

- 参数为重复的 `symbols`，不支持逗号分隔；
- 响应为以标的代码为键的 tick 对象字典；
- 无效或不存在的交易对会被服务端静默跳过，添加/替换校验必须显式确认目标代码出现在有效报价中；旧配置中的无效项仍显示 `--`；
- 外汇休市时返回 200 与最后一笔报价，`marketState` 为 `closed`，插件照常显示；
- 匿名限流 15000 次/分钟/IP，远高于插件刷新频率。

### Bitget CFD

`MarketClient` 将非 biquote 请求路由到 `api.bitget.com/api/v3/cfd/market/tickers`。
ECN / Standard 多标的一次无参数请求，全量结果按精确模式及配置代码筛选；
无参数接口实测不含 `.pro`，Pro 必须逐标的请求，任何失败均报告错误，不跨来源或模式回退。
单标的使用 `?symbol=EURUSD` / `EURUSD.s` / `EURUSD.pro`。

`ParseBitgetCfdResponse` 检查 `code=00000` 与 data 数组；`bid1` / `ask1` 为正且卖价不低于买价，
中间价取均值。`bidOpenPriceChange` 为比率，乘 100 后存入 `dailyChangePercent`；
这是买价日开盘口径（不同于 biquote 前收），缺失/非法值保持空，不从 ask 涨跌伪造。
当前只接受六位字母代码，覆盖外汇及 XAUUSD 等贵金属，不声称支持所有 CFD 品种。

[Bitget 文档](https://www.bitget.com/docs/catalog/cfd-market/cfd-market)标注只读权限，
当前匿名实测可读但不保证长期开放；权限收紧时显示错误，不存储交易凭据。
[Bybit 文档](https://bybit-exchange.github.io/docs/v5/tradfi-integration#cfd-mt5-products)
明确 CFD 属于 MT5、不支持 V5，因此不使用永续替代。

#### CFD WebSocket 核实结果

当前公开 CFD 行情目录只列出 REST tickers 和历史 K 线。
[UTA WebSocket Tickers Channel](https://www.bitget.com/docs/uta/websocket/public/Tickers-Channel)
的 `instType` 列举 spot / usdt-futures / coin-futures / usdc-futures，没有 CFD；
[2026 年 9 月 CFD 上线记录](https://www.bitget.com/docs/uta/changelog/2026-09)
也没有给出 CFD WebSocket 地址或订阅协议。不能据此断言平台内部不存在推送，但目前不足以实现可靠的公开 CFD WS 客户端。
因此保持 REST 轮询及 `RefreshSeconds` 生效，不猜测频道、不用普通合约价格替代。
若后续获得可验证的 CFD 专用协议，才添加推送路径：WS 模式停用轮询间隔，由推送更新 pending 缓存，
在宿主回调中发布显示快照；需同时实现心跳、可取消接收、退避重连、配置 generation 隔离及周末暂停。

## 7. 显示项规则

- `GetItem(0)` 是汇总项，ID 为 `FxPriceSummary`。
- `GetItem(1..N)` 是各交易对独立项，ID 为 `FxPriceItem0..7`。
- `GetItemValueText()` 只返回缓存，不能访问网络。
- 任务栏标的名按来源模式显示为 `EURUSD` / `EURUSD.s` / `EURUSD.pro`；独立项采用自绘以绕过 TrafficMonitor 按 ItemId 缓存旧标签的问题。
- 任务栏格式化拼装勾选的字段：`买:1.15823 卖:1.15829 中:1.15826 涨跌:-0.61%`；汇总项在主价格后追加日涨跌幅，也支持仅显示涨跌幅。涨跌幅固定两位小数并保留正负号，缺失时显示 `--`。

## 8. 配置文件

新配置文件为 TrafficMonitor 插件配置目录下的 `FxPrice.ini`：

```ini
[General]
Source=biquote
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

- `Source` 为 `biquote` / `bitget-ecn` / `bitget-standard` / `bitget-pro`；缺失/未知回退 biquote，兼容旧配置；
- `RefreshSeconds` 范围为 2–300；
- `ShowBid/ShowAsk/ShowMid/ShowChange` 为全局显示开关（0/1），其中 `ShowChange` 同时控制任务栏独立项、汇总项和工具提示中的日涨跌幅；
- `SymbolN` 为该交易对代码，`PrecisionN` 为该交易对的小数位数（-1 自动，0–10 固定）；
- 新配置缺少 `PrecisionN` 时兼容读取旧版 `General.DisplayPrecision`；两者都缺失或精度值非法时回退 -1（自动推断）；
- `Count` 范围为 1–8。

## 9. 构建

默认构建链路为 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe`。以下命令通过
Visual Studio 生成器创建工程，再由 MSBuild 调用 MSVC 编译器；当前不使用 Clang。

x64：

```powershell
cmake --preset x64
cmake --build --preset x64-release
ctest --preset x64-release
```

Win32：

```powershell
cmake --preset win32
cmake --build --preset win32-release
ctest --preset win32-release
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
