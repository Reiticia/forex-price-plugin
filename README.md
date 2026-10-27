# 外汇行情插件

用于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的外汇行情插件。插件通过 [biquote.io](https://biquote.io/docs/) 公开 API 读取报价，不需要注册、登录、账户 ID 或 API Key。

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
- 一次请求批量获取全部交易对（`/api/latest?symbols=...`），匿名限流 15000 次/分钟，远高于插件刷新频率；
- 配置保存到 `FxPrice.ini`，不包含任何账户或令牌字段；
- 支持 TrafficMonitor Plugin API v8。

## 配置界面

配置界面沿用 `crypto-price-plugin` 的列表式编辑方式：

1. 上方列表显示所有已监控交易对；
2. 选择列表项后，可以在“交易对”输入框中修改并点击“更新选中项”；
3. 输入新交易对后点击“添加新标的”；
4. 可删除选中项并配置 2–300 秒刷新间隔；
5. 在“任务栏内容”勾选买一价、卖一价、中间价和日涨跌幅；开关同时作用于工具提示；
6. 在“小数位数”为当前选中的交易对填写 0–10 位固定显示位数，留空则由程序按行情自动判断（每个交易对独立保存）；
7. 数据源固定为 `biquote.io`。

支持以下输入形式，保存时统一规范化：

```text
EURUSD.s
EUR/USD
EUR-USD
eurusd
```

以上均保存为内部代码 `EURUSD`，界面和任务栏显示为 `EURUSD.s`。

## 构建

需要 Visual Studio 2022（含“使用 C++ 的桌面开发”工作负载）和 CMake 3.20 或更高版本。

默认构建链路为 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe`。CMake 负责生成
Visual Studio 工程，实际 C++ 编译由 MSVC 完成；当前不使用 Clang。

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

构建产物：

```text
build-x64/Release/FxPricePlugin.dll
build-win32/Release/FxPricePlugin.dll
```

`FxPriceLiveSmoke.exe` 可执行一次真实 biquote 请求；它不加入默认 `ctest`，避免离线构建被网络状态影响。

### GitHub Actions

[构建工作流](.github/workflows/build.yml)同时支持发布构建和手动构建：

- 推送 `vX.Y.Z` Tag 时，工作流会验证 Tag、CMake 项目版本、插件元数据、HTTP
  User-Agent 与 `CHANGELOG.md` 版本一致，构建并测试 x64、Win32，从变更日志提取对应
  版本章节作为 Release 说明，随后创建 GitHub Release，附带两个 DLL 和 SHA-256 校验文件；
- 在 Actions 页面手动运行时，只构建并测试 x64、Win32，不创建 Release。两个 DLL 作为
  Workflow Artifacts 保留 14 天，可从该次运行页面下载。

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
- 无效或不存在的交易对会被服务端静默跳过，此时对应显示项显示 `--`，工具提示给出错误信息；
- 网络代理、TLS 或区域访问限制都可能导致刷新失败；
- 插件将单次响应限制为 1 MiB，并配置连接、发送和接收超时；
- 若对数据稳定性、正式 SLA 或严格滚动 24 小时计算有要求，应改用需要认证的正式行情提供商。

## 许可证

MIT License，详见 [LICENSE](LICENSE)。

## 参考

- [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97)
- [crypto-price-plugin](https://github.com/Reiticia/crypto-price-plugin)
- [biquote.io API 文档](https://biquote.io/docs/)
