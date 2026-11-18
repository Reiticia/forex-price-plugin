# Changelog

本项目所有重要变更都会记录在此文件中。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增

- 接入 Bitget CFD ECN / 零手续费 `.s` / Pro `.pro` 行情，保留 biquote 默认来源；设置中切换来源并校验全部标的，INI 保存模式
- Bitget 精确匹配模式的买卖价及中间价，买价日开盘涨跌比率转换为百分比，缺失值显示 `--`；ECN / `.s` 批量查询，Pro 逐标的请求
- 补充解析、来源切换/失败回滚、配置兼容和真实 WinHTTP 测试入口；记录 Bybit MT5 CFD 不支持 V5 的官方限制，不用永续行情替代

### 修复

- 精简外汇悬停提示，保留勾选的买一／卖一／中间价及日涨跌幅，去掉逐行英文描述；限制总长为 384 个 UTF-16 单元并截断错误详情，避免与其他插件合并后触发 TrafficMonitor 1.85.1 的 MFC 1024 字符上限，反复弹出“遇到不适当的参数”

### 变更

- 使用统一的 CMake Presets，本地和 GitHub Actions 均输出至 `build/x64/Release/` 或 `build/win32/Release/`；构建最低要求为 CMake 3.21
- 恢复任务栏独立项和汇总项的日涨跌幅显示，沿用现有显示开关并同步预留宽度

### 计划

- 根据 biquote.io 接口变化继续完善行情解析和兼容性测试

## [0.4.0]

### 新增

- 小数位数按交易对独立配置，留空时按行情自动推断，也可固定为 0–10 位
- 列表式配置窗口支持新增、修改、删除交易对
- 增加 biquote.io 离线解析、格式化、配置和 DLL 烟雾测试
- 增加 x64、Win32 双架构 GitHub Actions 构建与发布流程
- 按开发、测试、部署三个主题整理项目文档

### 变更

- 统一使用 `CMake → Visual Studio 2022 / MSBuild → MSVC CL.exe` 构建链路
- 配置格式使用 `FxPrice.ini`，不保存账户、令牌或 API Key

### 修复

- x64 与 Win32 并行执行测试时使用独立临时目录，避免配置迁移用例互相干扰
- 发布工作流仅接受唯一的有效版本声明，避免注释或重复声明绕过版本一致性门禁

## [0.3.0]

### 新增

- 首个版本：TrafficMonitor 外汇行情插件
- 支持 biquote.io 公共行情接口
- 支持买一价、卖一价、中间价和日涨跌幅显示
