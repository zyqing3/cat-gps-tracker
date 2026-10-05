# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目背景

- 目标：宠物猫用的 GPS 定位项圈 —— GPS 定位、4G 无线公网数据上传、手机 App 查看实时定位信息与运动轨迹。
- 允许基于开源项目，也可从零开始。
- 验收标准：可运行的 demo 实物。
- 分工边界：Claude 负责规划、计划、实施；用户负责购买元器件、开发设备等。
- 需求说明见 `spec_gps.md`（中文）。

## 当前状态

- 仓库尚无代码，只有 `spec_gps.md`；硬件选型、固件、手机 App、通信方案均未确定。
- 构建、烧录、测试命令与代码架构尚未存在 —— 待项目落地后补充到本文件中。

## 沟通说明

- 用户是初中生：解释、文档和沟通应通俗易懂，避免堆砌专业术语。
- 需求文档和沟通使用中文。

## Agent skills

### Issue tracker

任务和问题记录在 GitHub Issues（用 `gh` 命令操作）。详见 `docs/agents/issue-tracker.md`。

### Domain docs

单上下文布局（single-context）：根目录的 `CONTEXT.md` + `docs/adr/`。详见 `docs/agents/domain.md`。
