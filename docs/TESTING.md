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
- 隐藏某个价格字段后示例文本随之变化。

## 2. 自动化测试

| 测试 | 联网 | 覆盖内容 |
| --- | --- | --- |
| `FxPriceTests` | 否 | 解析、格式化、配置及传输抽象 |
| `FxPricePluginSmoke` | 否 | DLL、API v8 显示项 |
| `FxPriceLiveSmoke` | 是 | 真实 biquote 请求 |
| `FxPricePluginLiveSmoke` | 是 | 加载 DLL 端到端刷新 |
| `FxPriceOptionsPreview` | 交互 | 打开 Win32 配置窗口进行人工验收 |

默认 CTest 不执行联网测试（`FxPriceLiveSmoke` 与 `FxPricePluginLiveSmoke` 需手动运行），避免网络波动造成误报。

## 3. 构建与离线测试

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

两个离线测试（`FxPriceTests`、`FxPricePluginSmoke`）都应显示 `Passed`。

## 4. 解析测试范围

`FxPriceTests` 验证：

- `EUR/USD`、`EUR-USD`、`eurusd`、`EURUSD.s` 规范化成 `EURUSD`，显示为 `EURUSD.s`；
- 六位大写字母校验与非法标的拒绝；
- `/api/latest?symbols=...` 重复参数构造与无效标的跳过；
- biquote tick 字典解析（bid/ask/mid/dayDiffPercent）；
- 中间价缺失时 `(bid+ask)/2` 回退；
- 非法（非对象、零价、缺买价）响应拒绝；
- 服务端返回非法标的代码时跳过；
- 显示开关与固定精度（0、2、3 位）格式化，任务栏独立项及汇总项按勾选显示涨跌幅；
- 详情尾部悬空分隔处理；
- 配置保存/读取：显示开关、每交易对独立小数位、脏精度键回退自动。

## 5. DLL 烟雾测试

```powershell
.\build-x64\Release\FxPricePluginSmoke.exe `
  .\build-x64\Release\FxPricePlugin.dll
```

验证内容：

- 未修饰导出名 `TMPluginGetInstance`；
- `GetAPIVersion()` 返回 8；
- 汇总项与独立显示项存在；
- 插件可以安全卸载。

## 6. 真实行情测试

```powershell
.\build-x64\Release\FxPriceLiveSmoke.exe
```

- 对 `EURUSD`、`USDJPY` 发起真实 biquote 请求；
- 校验返回标的含 `EURUSD`；
- 打印 bid/ask/mid/日涨跌；
- 进程退出码为 0。

```powershell
.\build-x64\Release\FxPricePluginLiveSmoke.exe `
  .\build-x64\Release\FxPricePlugin.dll
```

- 加载 DLL，调用 `DataRequired` 触发后台刷新；
- 校验独立项 value 含 `◀`、`▶`、`≈`、`Δ`；
- 工具提示包含 `日涨跌`。

真实测试最多等待约 20 秒，覆盖 biquote 网络请求往返。

## 7. DLL 导出和架构

```powershell
dumpbin /exports .\build-x64\Release\FxPricePlugin.dll
```

或：

```powershell
llvm-readobj --coff-exports .\build-x64\Release\FxPricePlugin.dll
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
7. 在隔离的测试环境中验证固定 UTC+3 周末边界：UTC 周五 20:59:59 仍可刷新，21:00:00 开始暂停；UTC 周日 20:59:59 仍暂停，21:00:00 恢复。暂停期间定时刷新、手动刷新、保存配置均不发起新的 biquote 请求；进入周末时原有报价保留，工具提示显示“周末休市，已暂停抓取（UTC+3）”。周末启动或修改配置后，没有缓存的项目显示 `--`。
   恢复时确认下次 `DataRequired` 自动抓取并移除暂停提示；保持 UTC 时刻不变并切换电脑时区，暂停判断应保持一致。周末前已发出的请求允许正常完成；测试结束后恢复系统时间和时区。
8. 断网后确认显示 `--`，工具提示包含 biquote 网络错误。

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
| 所有行情为 `--` | 检查 `biquote.io:443`、代理、防火墙和 TLS |
| 个别交易对显示 `--` | 检查交易对是否为六位大写货币代码，服务端可能静默跳过无效标的 |
| 小数位数与设置不一致 | 确认先选中对应交易对，再点击“更新选中项”或“确定” |
| 实时测试超时 | 检查 DNS、代理、防火墙和 biquote 服务状态 |
| 配置无法保存 | 检查 TrafficMonitor 插件配置目录写权限 |
