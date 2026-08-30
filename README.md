# 外汇行情插件

用于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的外汇行情插件。插件通过 [biquote.io](https://biquote.io/docs/) 公开 API 读取报价，不需要注册、登录、账户 ID 或 API Key。

> biquote.io 是免费公开行情服务，并非面向第三方开发者承诺长期兼容的正式商业 API，可能调整字段或限流。本插件只读取公开行情，不访问账户，也不执行交易。

## 功能

- 同时监控最多 8 个外汇交易对；
- 每个交易对显示买一价、卖一价、中间价和日涨跌幅；
- 中间价优先取服务端 `mid` 字段，缺失时按 `(买一价 + 卖一价) / 2` 计算；
- 日涨跌幅取 biquote `dayDiffPercent` 字段，即相对前收的变化，并非严格滚动 24 小时；
- 提供所有交易对汇总项和各交易对独立显示项；
- 后台线程批量刷新，不阻塞 TrafficMonitor 界面；
- 一次请求批量获取全部交易对（`/api/latest?symbols=...`），匿名限流 15000 次/分钟，远高于插件刷新频率；
- 配置保存到 `FxPrice.ini`，不包含任何账户或令牌字段；
- 支持 TrafficMonitor Plugin API v8。

## 配置界面

配置界面沿用 `crypto-price-plugin` 的列表式编辑方式：

1. 上方列表显示所有已监控交易对；
2. 选择列表项后，可以在“交易对”输入框中修改并点击“更新选中项”；
3. 输入新交易对后点击“添加新标的”；
4. 可删除选中项并配置 2–300 秒刷新间隔；
5. 数据源固定为 `biquote.io（无 Key）`。

支持以下输入形式，保存时统一规范化：

```text
EUR/USD
EUR-USD
eurusd
```

以上均保存为内部代码 `EURUSD`，界面显示为 `EUR/USD`。

## 构建

需要：

- Windows 10 或更高版本；
- Visual Studio 2022，并安装“使用 C++ 的桌面开发”；
- CMake 3.20 或更高版本。

### x64

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

### Win32

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

## 安装和使用

1. 完全退出 TrafficMonitor；
2. 根据 TrafficMonitor 位数，将对应的 `FxPricePlugin.dll` 复制到 `TrafficMonitor/plugins`；
3. 启动 TrafficMonitor；
4. 在插件管理中打开“外汇行情”的选项；
5. 添加交易对并设置刷新间隔；
6. 在 TrafficMonitor 显示设置中启用“外汇行情汇总”或所需交易对的独立显示项。

任务栏独立项示例：

```text
EUR/USD: 买 1.15823 卖 1.15829 中 1.15826 -0.61%
```

## 数据与异常说明

- 外汇市场休市时（周末等），接口返回 200 和最后一笔有效报价，`marketState` 为 `closed`，本插件照常显示最后一笔行情；
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
