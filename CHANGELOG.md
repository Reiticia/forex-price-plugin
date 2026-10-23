# Changelog

本项目所有重要变更都会记录在此文件中。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 变更

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
