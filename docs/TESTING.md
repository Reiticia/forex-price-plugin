# 测试文档

## 1. 测试目标

测试覆盖：

- 固定 UTC+3 周末暂停窗口及 UTC 周五/周日 21:00 的切换边界；
- biquote.io `/api/latest` 响应解析；
- 交易对符号规范化与去重；
- 买一价、卖一价、中间价、日涨跌幅读取；
- 中间价缺失时按买卖价均值回退；
- 无效或非法标的的跳过；
- 显示开关与每交易对独立小数位；
- 配置保存与读取（含脏值回退）；
- DLL 导出入口、API v8 与多交易对显示项；
- 隐藏某个价格字段后示例文本随之变化；
- 悬停提示在 8 标的、超长错误、空列表和周末暂停状态下均不超过 384 个 UTF-16 单元，为其他插件及宿主预留空间。

## 2. 自动化测试

| 测试 | 联网 | 覆盖内容 |
| --- | --- | --- |
| `FxPriceTests` | 否 | 解析、格式化、配置及传输抽象 |
| `FxPricePluginSmoke` | 否 | DLL、API v8 显示项 |
| `FxPriceRuntimeTests` | 仅本机 | 不可变指针并发、旧 generation 拒绝、本机阻塞 TLS 取消/总超时、Pro 共用预算及配置取消 |
| `FxPriceControlledUnload` | 否 | 延迟模拟传输的测试 DLL，反复加载/卸载及请求中卸载，不受周末影响 |
| `FxPriceLifecycleTests` | 可能 | 真实 DLL 反复加载/卸载及请求中释放宿主引用；不依赖请求成功 |
| `FxPriceOptionsTests` | 否 | 注入模拟传输，验证 Win32 添加、替换、直接保存及取消流程 |
| `FxPriceLiveSmoke` | 是 | 真实 biquote 请求 |
| `FxPricePluginLiveSmoke` | 是 | 加载 DLL 端到端刷新 |
| `FxPriceOptionsPreview` | 交互 | 打开 Win32 配置窗口进行人工验收 |

默认 CTest 不依赖外部行情成功；`FxPriceLifecycleTests` 在工作日可能发起一次实际请求，只验证模块最终安全卸载（请求预算 20 秒，测试上限 35 秒）。真实报价成功测试 `FxPriceLiveSmoke` 与 `FxPricePluginLiveSmoke` 需手动运行。

## 3. 构建与离线测试

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

六个 CTest 均应显示 `Passed`。配置窗口测试自动打开并关闭窗口，注入传输而不访问外部网络。
Runtime 测试使用临时本机 TCP 监听模拟不响应的 TLS 对端，验证进行中的 WinHTTP 在取消/截止时间后退出并排空回调。
ControlledUnload 使用仅测试构建的 DLL 模拟慢请求；生产 DLL 无测试导出。

## 4. 解析测试范围

`FxPriceTests` 验证：

- `EUR/USD`、`EUR-USD`、`eurusd`、`EURUSD.s` 规范化成 `EURUSD`，显示为 `EURUSD.s`；
- 六位大写字母校验与非法标的拒绝；
- `/api/latest?symbols=...` 重复参数构造与无效标的跳过；
- biquote tick 字典解析（bid/ask/mid/dayDiffPercent）；
- 中间价缺失时 `(bid+ask)/2` 回退；
- 非法（非对象、零价、缺买价）响应拒绝；
- 服务端返回非法标的代码时跳过；
- 单标的校验：目标报价必须存在，不能以其他标的报价替代；空响应、非法 JSON、缺失/零/负价格及网络失败均拒绝；
- 显示开关与固定精度（0、2、3 位）格式化，任务栏独立项及汇总项按勾选显示涨跌幅；
- 任务栏宽度样本按标的量级生成：EURUSD 一位整数、USDJPY 三位整数、XAUUSD 四位整数；支持 0–10 位固定精度、大数值货币对及紧凑涨跌样本，DLL 冒烟检查独立项/汇总项均使用新样本；
- 详情尾部悬空分隔处理；
- 配置保存/读取：显示开关、每交易对独立小数位、脏精度键回退自动。

## 5. DLL 烟雾测试

```powershell
.\build\x64\Release\FxPricePluginSmoke.exe `
  .\build\x64\Release\FxPricePlugin.dll
```

验证内容：

- 未修饰导出名 `TMPluginGetInstance`；
- `GetAPIVersion()` 返回 8；
- 汇总项与独立显示项存在；
- 插件可以安全卸载。

## 6. 真实行情测试

```powershell
.\build\x64\Release\FxPriceLiveSmoke.exe
```

- 对 `EURUSD`、`USDJPY` 发起真实 biquote 请求；
- 校验返回标的含 `EURUSD`；
- 打印 bid/ask/mid/日涨跌；
- 进程退出码为 0。

```powershell
.\build\x64\Release\FxPricePluginLiveSmoke.exe `
  .\build\x64\Release\FxPricePlugin.dll
```

- 加载 DLL，调用 `DataRequired` 触发后台刷新；
- 校验独立项 value 含 `◀`、`▶`、`≈`、`Δ`；
- 工具提示包含 `日涨跌` 与所选价格字段，且总长不超过 384 个 UTF-16 单元。

真实测试最多等待约 20 秒，覆盖 biquote 网络请求往返。

### Bitget CFD

```powershell
.\build\x64\Release\FxPriceLiveSmoke.exe bitget-ecn
.\build\x64\Release\FxPriceLiveSmoke.exe bitget-standard
.\build\x64\Release\FxPriceLiveSmoke.exe bitget-pro
```

DLL 端到端测试也接受来源参数，并使用临时 INI（退出时清理，不修改实际插件配置）：

```powershell
.\build\x64\Release\FxPricePluginLiveSmoke.exe .\build\x64\Release\FxPricePlugin.dll bitget-ecn
.\build\x64\Release\FxPricePluginLiveSmoke.exe .\build\x64\Release\FxPricePlugin.dll bitget-standard
.\build\x64\Release\FxPricePluginLiveSmoke.exe .\build\x64\Release\FxPricePlugin.dll bitget-pro
```

端到端刷新仍遵守周末暂停，需在暂停窗口外运行。
三种模式均使用 WinHTTP 实际请求并检查 EURUSD。ECN / Standard 批量查询，Pro 逐标的。
Pro 日涨跌字段可能为空，应显示 `--` 而非 0%。匿名访问失败时检查权限、地区、限流和网络；
不能把认证要求当成不存在接口。默认 CTest 不依赖这些请求。

新增离线测试覆盖精确模式筛选、比率转百分比、买卖均值、缺失涨跌、错误响应、非法价格、
数据源持久化及旧配置回退；Win32 测试验证切换来源成功、权限失败、目标标的缺失时回滚，
并检查校验中禁用来源及保存按钮。

手工验收：分别切换三种模式，确认显示后缀/工具提示来源、涨跌口径正确，重启后保持来源；
先以 EURUSD 再加入 XAUUSD，断网及切换失败不能混入旧来源报价。

## 7. DLL 导出和架构

```powershell
dumpbin /exports .\build\x64\Release\FxPricePlugin.dll
```

或：

```powershell
llvm-readobj --coff-exports .\build\x64\Release\FxPricePlugin.dll
```

必须包含：

```text
TMPluginGetInstance
```

同时确认 x64 产物为 `x86_64`，Win32 产物为 `i386`。

## 8. TrafficMonitor 手工验收

1. 安装 `FxPricePlugin.dll`，插件管理应显示“外汇行情”0.4.0。
2. 配置 `EUR/USD` 和 `USD/JPY`，分别设置自动精度与固定精度。
3. 确认汇总项和每个交易对的独立显示项都能启用。
4. 依次切换买一价、卖一价、中间价和日涨跌幅开关，确认任务栏独立项、汇总项及工具提示同步变化，涨跌幅未被截断。
5. 确认每个交易对按自身小数位设置显示，留空时跟随行情精度。
6. 重启 TrafficMonitor，确认交易对、显示开关和精度配置被保留。
   提示以 `◀`、`▶`、`≈`、`Δ` 紧凑显示所选字段，顶部说明符号含义及日涨跌口径，不重复英文全名；总长超过预算时按完整行截断并显示“更多见插件设置”。
7. 在隔离的测试环境中验证固定 UTC+3 周末边界：UTC 周五 20:59:59 仍可刷新，21:00:00 开始暂停；UTC 周日 20:59:59 仍暂停，21:00:00 恢复。暂停期间定时刷新、手动刷新、保存未变更标的的配置均不发起新的 biquote 刷新请求；添加或替换标的仍会主动查询一次报价用于有效性校验，校验结果不写入任务栏报价缓存；进入周末时原有报价保留，工具提示显示“周末休市，已暂停抓取（UTC+3）”。周末启动或修改配置后，没有缓存的项目显示 `--`。
   恢复时确认下次 `DataRequired` 自动抓取并移除暂停提示；保持 UTC 时刻不变并切换电脑时区，暂停判断应保持一致。周末前已发出的请求允许正常完成；测试结束后恢复系统时间和时区。
8. 断网后确认显示 `--`，工具提示包含 biquote 网络错误。
9. 输入格式合法但不存在的 `ABCDEF`，分别点击“添加新标的”、“更新选中项”和直接点击“确定”，确认均提示校验失败、列表及持久化配置不变；断网或接口无有效报价时同样拒绝。恢复网络后用有效标的重试，确认可添加并保存。
10. 查询期间确认窗口仍响应，重复点击不能绕过校验；点击取消或关闭时主动中止请求，安全收尾后关闭且不保存。仅修改原标的精度时，离线也应可保存。

### 任务栏右侧留白回归

替换 DLL 后完全重启 TrafficMonitor，以触发宿主重新测量（仅刷新价格不会立即重排）。
分别启用独立项和汇总项，检查 EURUSD、USDJPY、XAUUSD 混合显示，
自动精度及固定精度 0 / 5 / 10，关闭部分价格字段和仅显示涨跌幅。
确认右侧不再按每个价格五位整数预留空白；价格、负号、百分号仍完整。
样本为稳定布局预留常见量级，并非任意极端价格的长度保证；首次布局后的异常跨量级变化仍受宿主重排时机限制。

## 9. 建议回归场景

- 测试 1、2 和 8 个交易对；
- EURUSD.s、USDJPY.s 等不同价格精度的交易对混合配置；
- 同一交易对使用斜杠、横杠和小写输入，确认规范化及去重；
- 小数位数分别设置为留空、0、2、5、10；
- 买一价、卖一价、中间价全部关闭或只开启一项，并切换日涨跌幅（包括仅显示涨跌幅）；
- biquote 返回缺少 `mid` 时按买卖价均值回退；
- 无效交易对、非法价格、HTTP 非 200、TLS、代理和超时；
- 连续快速保存配置，不能显示上一代配置的迟到响应。

## 10. 常见失败

| 现象 | 排查方法 |
| --- | --- |
| CMake 找不到编译器 | 安装 Visual Studio C++ 桌面开发工作负载 |
| `LoadLibrary failed: 193` | DLL 与测试程序或 TrafficMonitor 架构不一致 |
| 反复弹出“遇到不适当的参数” | TrafficMonitor 1.85.1 的 MFC 合并提示有 1024 字符上限；升级外汇和其他插件至精简提示版本，仍超限时关闭主窗口和任务栏鼠标提示 |
| 所有行情为 `--` | 检查 `biquote.io:443`、代理、防火墙和 TLS |
| 个别交易对显示 `--` | 检查交易对是否为六位大写货币代码，服务端可能静默跳过无效标的 |
| 小数位数与设置不一致 | 确认先选中对应交易对，再点击“更新选中项”或“确定” |
| 实时测试超时 | 检查 DNS、代理、防火墙和 biquote 服务状态 |
| 配置无法保存 | 检查 TrafficMonitor 插件配置目录写权限 |
