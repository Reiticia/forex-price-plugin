# 部署文档

## 1. 部署物

生产环境只需要：

```text
FxPricePlugin.dll
```

不要部署测试程序、`.lib`、`.exp`、`.pdb`、源码或整个构建目录。

## 2. 网络要求

放行 HTTPS 443：

| 用途 | 域名 |
| --- | --- |
| biquote 外汇行情 | `biquote.io` |
| Bitget CFD 行情 | `api.bitget.com` |

插件只访问公共行情接口，不需要配置 API Key、账户 ID 或访问令牌。

## 3. 发布构建

默认构建链路为 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe`。

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

GitHub Release 按架构提供发布包：

```text
FxPricePlugin-x64.zip
FxPricePlugin-win32.zip
```

每个 ZIP 内的文件名均为 `FxPricePlugin.dll`，不携带版本号、提交号或架构后缀。
下载对应架构并解压后即可安装，无需重命名。发布前可记录 DLL 哈希：

```powershell
Get-FileHash .\build\x64\Release\FxPricePlugin.dll -Algorithm SHA256
```

GitHub Actions 在推送匹配 `vX.Y.Z` 的 Tag 时先校验 Tag、CMake 项目版本、插件元数据、
WinHTTP User-Agent 与 `CHANGELOG.md` 版本一致，再自动构建 x64、Win32，运行离线测试，
按架构打包 ZIP，生成 ZIP 的 SHA-256 校验文件并创建 Release；手动运行工作流时只保留
`FxPricePlugin-x64`、`FxPricePlugin-win32` 两个 Workflow Artifacts，内部均为
`FxPricePlugin.dll`，不创建 Release。

## 4. 首次安装

1. 完全退出 TrafficMonitor。
2. 确认 TrafficMonitor 架构，选择对应的 DLL。
3. 将 `FxPricePlugin.dll` 复制到 TrafficMonitor 的 `plugins` 文件夹。
4. 启动 TrafficMonitor。
5. 在“其他功能 → 插件管理”中确认“外汇行情”0.4.0 已加载。
6. 打开选项，添加要监控的外汇交易对。
7. 为每个交易对设置独立的小数位数（留空自动判断）。
8. 在 TrafficMonitor 显示设置中启用汇总项或独立项。

汇总示例：

```text
外汇: EURUSD.s 1.15826 -0.61% | GBPUSD.s 1.35349 +0.25%
```

## 5. 显示项 ID

显示项 ID 为 `FxPriceSummary`（汇总项）和 `FxPriceItem0..7`（独立项）。TrafficMonitor
在“显示设置”中按 ID 保存勾选状态；更换 ID 后需要重新勾选要显示的项。

## 6. 配置文件

新配置文件：

```text
FxPrice.ini
```

关键配置：

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

`PrecisionN` 为每个交易对独立的小数位数：-1 表示自动判断，0–10 表示固定位数。
新配置缺少 `PrecisionN` 时兼容读取旧版 `General.DisplayPrecision`；两者都缺失或精度值非法时回退 -1（自动推断）。
`Count` 范围为 1–8，插件至少保留一个交易对。

## 7. 部署后验证

- 插件管理显示“外汇行情”0.4.0；
- 任务栏和工具提示显示所勾选的买一价、卖一价、中间价和日涨跌幅；
- 各交易对按各自设置的小数位数显示；
- 外汇休市时仍显示最后一笔有效报价；
- 重启 TrafficMonitor 后配置仍有效；
- 任务栏没有因网络请求出现卡顿。

## 8. 回滚

1. 完全退出 TrafficMonitor。
2. 移除 `FxPricePlugin.dll`。
3. 恢复备份的旧版 `FxPricePlugin.dll`。
4. 启动 TrafficMonitor 并检查版本和行情。

## 9. 故障排查

| 现象 | 原因和处理 |
| --- | --- |
| 反复弹出“遇到不适当的参数” | TrafficMonitor 1.85.1 合并所有提示后超过 MFC 的 1024 字符上限；升级到精简提示的外汇及其他插件版本。外汇提示限制为 384 个 UTF-16 单元，但不能约束其他插件；仍超限时关闭主窗口和任务栏的鼠标提示 |
| 插件管理中没有插件 | 检查 DLL 路径及 x64/Win32 架构 |
| Windows 错误 193 | DLL 与 TrafficMonitor 架构不一致 |
| 所有行情为 `--` | hover 查看错误，检查 biquote.io 连通性、防火墙和代理 |
| 个别交易对显示 `--` | 该交易对可能无效或已被服务端静默跳过；检查交易对代码 |
| 价格小数位与预期不符 | 检查该交易对的 `PrecisionN`（-1 自动，0–10 固定） |
| 配置无法保存 | 检查 TrafficMonitor 插件配置目录写权限 |
| 汇总太宽 | 减少汇总交易对，或改用独立显示项布局 |

## 10. 安全与运维

- 不在 INI 中保存任何账户或令牌字段；
- 企业网络按所选来源放行 `biquote.io:443` 或 `api.bitget.com:443`；Bitget 当前匿名可读不代表长期保证，权限收紧会显示错误；
- 发布记录应包含版本、架构、构建时间和 SHA-256；
- 生产升级前应运行离线解析测试和真实行情测试。
