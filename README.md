# 外汇行情插件

用于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的外汇行情插件。插件支持 [biquote.io](https://biquote.io/docs/) 和 [Bitget CFD](https://www.bitget.com/docs/catalog/cfd-market/cfd-market) 行情，当前均以不带账户或 API Key 的只读请求接入。旧配置默认继续使用 biquote。

> biquote.io 是免费公开行情服务，并非面向第三方开发者承诺长期兼容的正式商业 API，可能调整字段或限流。本插件只读取公开行情，不访问账户，也不执行交易。

## 项目文档

- [开发文档](docs/DEVELOPMENT.md)：架构、数据源适配、配置格式和构建方法
- [测试文档](docs/TESTING.md)：解析测试、DLL 烟雾测试和真实行情测试
- [部署文档](docs/DEPLOYMENT.md)：安装、从旧版升级、回滚和故障排查

## 功能

- 同时监控 1–8 个外汇交易对；
- 任务栏每个交易对按勾选显示买一价、卖一价、中间价和日涨跌幅，工具提示同步显示；
- 中间价优先取服务端 `mid` 字段，缺失时按 `(买一价 + 卖一价) / 2` 计算；
- 日涨跌幅取 biquote `dayDiffPercent` 字段，即相对前收的变化，并非严格滚动 24 小时；
- 小数位数按交易对独立设置：留空由程序按行情自动推断，也可固定为 0–10 位；
- 提供所有交易对汇总项和各交易对独立显示项；
- 后台线程批量刷新，不阻塞 TrafficMonitor 界面；
- 按固定 UTC+3 时间在周六、周日暂停抓取（包括手动刷新），周一自动恢复；
- biquote 使用 `/api/latest?symbols=...` 批量获取；Bitget ECN / `.s` 也支持批量抓取，Pro 逐标的请求；
- 配置保存到 `FxPrice.ini`，不包含任何账户或令牌字段；
- 支持 TrafficMonitor Plugin API v8。

## 配置界面

配置界面沿用 `crypto-price-plugin` 的列表式编辑方式：

1. 上方列表显示所有已监控交易对；
2. 选择列表项后，可以在“交易对”输入框中修改并点击“更新选中项”；
3. 输入新交易对后点击“添加新标的”；后台查询到该标的的有效买卖报价后才加入列表，不存在、无报价或网络失败时拒绝添加并提示原因；
4. 可删除选中项并配置 2–300 秒刷新间隔；
5. 在“任务栏内容”勾选买一价、卖一价、中间价和日涨跌幅；开关同时作用于工具提示；
6. 在“小数位数”为当前选中的交易对填写 0–10 位固定显示位数，留空则由程序按行情自动判断（每个交易对独立保存）；
7. 在“数据源”选择 `biquote.io`、`Bitget CFD ECN`、`Bitget CFD .s` 或 `Bitget CFD Pro`。切换时后台验证列表中全部标的，失败则保持原来源；通过后点击“确定”保存。

支持以下输入形式，保存时统一规范化：

```text
EURUSD.s
EUR/USD
EUR-USD
eurusd
```

以上均保存为内部代码 `EURUSD`；也接受 `EURUSD.pro` 输入。实际请求及显示后缀由全局数据源模式决定：biquote / Bitget 零手续费模式显示 `.s`，Bitget ECN 无后缀，Pro 为 `.pro`，不同模式不会混用报价。

通过“更新选中项”或直接点击“确定”替换标的时也会校验报价；仅调整已有标的的小数位数或其他显示设置不需要联网。校验期间禁止修改标的或保存；点击取消会放弃操作，并在当前请求结束（或超时）后关闭窗口。

## Bitget / Bybit CFD 接口说明

- **Bitget CFD 已接入**：`GET https://api.bitget.com/api/v3/cfd/market/tickers`，使用 `bid1` / `ask1`，中间价取均值；日涨跌取 `bidOpenPriceChange × 100`，是买价相对日开盘的变化，不是滚动 24 小时，也不等同于 biquote 的相对前收口径。缺失涨跌（Pro 实测为空）显示 `--`。
- 支持六位字母的外汇 / 贵金属代码，例如 `EURUSD`、`USDJPY`、`XAUUSD`；本次未扩展股票、指数等非六位代码。
- Bitget 文档标注 `UTA trade (read-only)` 权限及 `10/sec/UID` 限流，但当前环境实测不带认证可读三种模式。**匿名访问并非长期保证**；若接口收紧权限，插件会报错，不自动切换来源，不采集或保存交易凭据。
- ECN / `.s` 多标的一次请求全量 tickers，再筛选配置；无 `symbol` 的响应实测不含 `.pro`，Pro 逐标的请求，弱网下取消/刷新等待可能更长。单标的校验使用精确 `symbol` 参数。
- **CFD WebSocket 尚无可核实的公开接入协议**：当前 CFD 行情目录仅列出 REST tickers / 历史 K 线；[WebSocket Tickers Channel](https://www.bitget.com/docs/uta/websocket/public/Tickers-Channel) 的 `instType` 仅包含 spot / usdt-futures / coin-futures / usdc-futures，不包含 CFD。因此本插件继续使用 REST 和刷新间隔，不能将普通合约频道当作 CFD 推送。此结论不代表 Bitget 网页内部或未来不会提供 CFD 推送服务。
- **Bybit CFD 暂未接入**：[官方 TradFi Integration](https://bybit-exchange.github.io/docs/v5/tradfi-integration#cfd-mt5-products) 明确说明其 CFD 属于 MT5，不能通过 V5 行情端点查询。`XAUUSD.s` CFD 和 `XAUUSDT` 永续不是同一产品。若需 Bybit CFD，应另做登录 MT5 终端的只读桥接；本项目未用永续行情冒充 CFD。
- 两个已接入来源沿用固定 UTC+3 周末暂停规则，不在周末持续请求；用户主动校验标的不受此限制。

## 构建

需要 Visual Studio 2022（含“使用 C++ 的桌面开发”工作负载）和 CMake 3.21 或更高版本。

默认构建链路为 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe`。CMake 负责生成
Visual Studio 工程，实际 C++ 编译由 MSVC 完成；当前不使用 Clang。

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

构建产物：

```text
build/x64/Release/FxPricePlugin.dll
build/win32/Release/FxPricePlugin.dll
```

两个项目使用相同的 `CMakePresets.json` 约定，本地和 CI 的构建目录统一为
`build/<架构>/`，Release 的 DLL、测试程序位于其 `Release/` 子目录；Debug 则位于
`Debug/`。请从新目录获取产物；旧构建目录不再使用，确认无需保留后可自行删除。

`FxPriceLiveSmoke.exe` 可执行一次真实 biquote 请求；它不加入默认 `ctest`，避免离线构建被网络状态影响。

### GitHub Actions

[构建工作流](.github/workflows/build.yml)同时支持发布构建和手动构建：

- 推送 `vX.Y.Z` Tag 时，工作流会验证 Tag、CMake 项目版本、插件元数据、HTTP
  User-Agent 与 `CHANGELOG.md` 版本一致，构建并测试 x64、Win32，从变更日志提取对应
  版本章节作为 Release 说明，随后创建 GitHub Release，附带 `FxPricePlugin-x64.zip`、
  `FxPricePlugin-win32.zip` 和 ZIP 的 SHA-256 校验文件；每个 ZIP 内均为 `FxPricePlugin.dll`；
- 在 Actions 页面手动运行时，只构建并测试 x64、Win32，不创建 Release。两个 DLL 作为
  `FxPricePlugin-x64`、`FxPricePlugin-win32` Workflow Artifacts 保留 14 天，可从该次运行页面下载，
  内部 DLL 名称统一为 `FxPricePlugin.dll`，不附加版本号、提交号或架构后缀。

## 安装和使用

1. 完全退出 TrafficMonitor；
2. 根据 TrafficMonitor 位数，将对应的 `FxPricePlugin.dll` 复制到 `TrafficMonitor/plugins`；
3. 启动 TrafficMonitor；
4. 在插件管理中打开“外汇行情”的选项；
5. 添加交易对并设置刷新间隔；
6. 在 TrafficMonitor 显示设置中启用“外汇行情汇总”或所需交易对的独立显示项。

任务栏独立项示例：

```text
EURUSD.s: ◀1.15823 ▶1.15829 ≈1.15826 Δ-0.61%
```

符号含义：`◀` 买一价、`▶` 卖一价、`≈` 中间价、`Δ` 日涨跌幅。设置窗口和工具提示仍保留中文说明。

## 数据与异常说明

- 按固定 UTC+3 时间从周六 00:00 到周一 00:00 暂停发起行情请求（即 UTC 周五 21:00 到周日 21:00），定时刷新、手动刷新和修改配置触发的刷新均不会绕过；进入周末时保留已显示的报价，工具提示显示暂停状态，周一首次刷新回调自动恢复；
- 周末启动插件时不会为了加载旧报价访问接口；没有缓存的交易对显示 `--`，修改配置后也需等到周一重新获取报价。进入周末前已发出的请求仍可正常结束；
- 此规则采用固定 UTC+3，与电脑本地时区无关，不随夏令时变化，也不包含节假日。其他时间若接口返回 `marketState: closed`，仍按正常刷新间隔读取最后一笔报价；
- 添加或替换标的时会主动查询报价进行有效性校验（包括周末，可使用接口返回的最后有效报价）；此查询仅用于校验，不更新任务栏缓存，也不改变周末暂停刷新的规则；
- 不存在或没有有效买卖报价的标的不能通过配置界面添加或替换；旧配置或手工修改 INI 中的无效标的仍可能显示 `--`；
- 网络代理、TLS 或区域访问限制都可能导致刷新失败；
- 插件将单次响应限制为 1 MiB，并配置连接、发送和接收超时；
- 若对数据稳定性、正式 SLA 或严格滚动 24 小时计算有要求，应改用需要认证的正式行情提供商。

## 许可证

MIT License，详见 [LICENSE](LICENSE)。

## 参考

- [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97)
- [crypto-price-plugin](https://github.com/Reiticia/crypto-price-plugin)
- [biquote.io API 文档](https://biquote.io/docs/)
