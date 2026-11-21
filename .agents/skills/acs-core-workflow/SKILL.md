---
name: acs-core-workflow
description: >-
  AI engineering core workflow, commandments (B1-B8, B-SEC, B-CMT, B-GIT), 5-stage execution model,
  L3/L4 planning, root-cause bug fixing (B7), 3-strike circuit breaker (B8), context memory archiving (B6),
  and multi-agent/MCP security protocol. Use when planning L3/L4 changes, diagnosing bugs, archiving long-context
  sessions, coordinating multi-agent swarms or MCP tools, or running pre-delivery self-checks.
  AI 工程核心工作流、行为铁律、L3/L4 计划、Bug 根因修复、三振熔断、上下文归档与多 Agent/MCP 安全协议。
license: MIT
metadata:
  domain: 00-行为规范
  version: "2.12.0"
  updated: "2026-11-20"
---

# ACS · Core Workflow (全局行为协议与五阶执行模型)

本 Skill 封装 `00-行为规范` 全套行为协议与标准交付模板，适用于所有工程任务的澄清、规划、实现、根因排障、验证与交付闭环。

## 核心执行原则

1. **需求先问后做 (B1)**：需求模糊、边界未定或存在设计分歧时，先列举选项向用户澄清，严禁盲猜。
2. **复杂任务先计划后编码 (B2)**：L3/L4 变更必须先按 [L3_PLAN_TEMPLATE.md](./assets/L3_PLAN_TEMPLATE.md) 提交多方案对比计划，获批后方可落盘；采用展开-收缩（Expand-Contract）渐进演进。
3. **最小变更与必要防御 (B3/B4)**：仅改动任务必需范围（允许安全清理确证无副作用的死代码）；仅在不可信外部边界校验，内部可信链路信任契约。
4. **真测试与客观验证 (B5)**：交付必须附带可复现的执行证据；核心业务模块行/分支覆盖率 $\ge 95\%$，严禁滥竽充数测试与 `sleep` 盲等。
5. **Bug 根因溯源与三振熔断 (B7/B8)**：严禁在报错点机械判空/吞异常；同一报错连续修复 3 次未果必须立即熔断，按 [B8_CIRCUIT_BREAKER_REPORT.md](./assets/B8_CIRCUIT_BREAKER_REPORT.md) 求助。
6. **工具安全与权限受控 (B-SEC/B-GIT)**：禁未经授权高危命令与私装依赖；排查探针用毕即焚；未获明确指令严禁 `git commit` / `git push` / PR / tag / release。

## 按场景加载详细规范 (`references/`)

| 任务场景 | 必读规范 | 配套交付模板 (`assets/`) |
|---|---|---|
| 核心铁律、L1~L4 任务分级、五阶状态机 (S1~S5) | [01-核心铁律与五阶执行模型.md](./references/01-核心铁律与五阶执行模型.md) | [L3_PLAN_TEMPLATE.md](./assets/L3_PLAN_TEMPLATE.md) · [B8_CIRCUIT_BREAKER_REPORT.md](./assets/B8_CIRCUIT_BREAKER_REPORT.md) |
| 上下文临近极限 / 阶段切换记忆归档 (B6) | [02-上下文归档机制.md](./references/02-上下文归档机制.md) | [MEMORY_SNAPSHOT_TEMPLATE.md](./assets/MEMORY_SNAPSHOT_TEMPLATE.md) |
| 严厉禁止清单、CoT 3 问与交付前 12 项预检 | [03-禁止清单与交付预检.md](./references/03-禁止清单与交付预检.md) | — |
| Bug 根因诊断、爆炸半径与同类缺陷全景排查 (B7) | [04-Bug根因诊断与全局修复.md](./references/04-Bug根因诊断与全局修复.md) | — |
| 命令行黑名单、防依赖幻觉、机密保护与工作区洁净 | [05-工具安全与环境守则.md](./references/05-工具安全与环境守则.md) | — |
| 多 Agent Swarm 接力信封、MCP 权限与提示词防注入 | [06-多智能体协作与工具协议.md](./references/06-多智能体协作与工具协议.md) | — |

## 跨 Skill 协作

- 95% 覆盖率门禁与性能基准细则见 `acs-engineering-baseline:references/03-测试控制流与速查.md`。
- Git 提交/推送/PR/发布逐项授权细则见 `acs-git-docs:references/01-Git协作与版本规范.md`。
- PR/代码审查五阶流水线见 `acs-code-review:references/01-代码审查规范.md`。

## 自动化洁净度与归档校验 (`scripts/`)

交付前可运行内置零依赖脚本核验工作区洁净度（`B-SEC` 探针/敏感文件）与 `.memory/INDEX.md` 索引完整性（`B6`）：

```bash
python3 ./scripts/check_workspace_hygiene.py <workspace_dir>
```

## 交付前核验

在输出最终答复或结束改动前，完整核对 [03-禁止清单与交付预检.md](./references/03-禁止清单与交付预检.md) 中的 CoT 3 问与 12 项交付 Checklist。
