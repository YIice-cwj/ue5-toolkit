---
name: acs-git-docs
description: >-
  Git collaboration, version control, release governance, and documentation/ADR standards:
  strict per-action authorization for commit/push/PR/merge/tag/release (B-GIT), Conventional Commits,
  branching & PR/MR workflows, merge & conflict resolution strategies, SemVer, tag/release & Keep a Changelog,
  Git LFS hygiene, README baseline, and immutable Architecture Decision Records (ADR).
  Use when preparing commits, branches, PRs, releases, CHANGELOGs, READMEs, or ADRs.
  Git 协作与文档治理规范：B-GIT 逐项授权、Conventional Commits、分支与合并、SemVer/发布、README 与 ADR 决策记录。
license: MIT
metadata:
  domain: 40-协作规范
  version: "2.5.0"
  updated: "2026-11-20"
---

# ACS · Git & Documentation Governance (版本协作与文档治理)

本 Skill 封装 `40-协作规范` 全部 2 篇文档与标准 ADR 模板，约束 Git 操作权限边界、发布流程及工程文档治理。

## 一票否决红线速查

- **操作逐项授权 (B-GIT)**：未获用户明确指令，严禁擅自 `git commit`、`git push`、创建/合并 PR、打 tag 或发布 release；**“提交/commit”仅授权本地提交，`git push` 及远端操作必须逐项另获明确指令**；严禁对共享分支 force push。
- **Conventional Commits**：提交信息严格采用 `<type>(<scope>): <subject>` 规范格式，禁止无意义提交信息（如 `fix`、`update`）。
- **SemVer 与 CHANGELOG**：版本号遵循语义化版本（破坏性变更升 Major）；CHANGELOG 遵循 Keep a Changelog，禁止直接粘贴裸 `git log`。
- **ADR 不可变原则 (`R-DOC-ADR-IMMUT`)**：L3/L4 架构决策与不可逆选型必须写入 `docs/adr/NNNN-title.md`；已 `Accepted` 的 ADR 严禁改写历史，变更时新建 ADR 标注 `Supersedes`。
- **文档新鲜度与零双源**：README 命令必须真实可执行；严禁在文档中残留过期链接、真实密钥/Token 或 PII。

## 按主题加载详细规范 (`references/`)

| 涉及主题 | 必读规范 | 配套模板 (`assets/`) |
|---|---|---|
| B-GIT 权限边界、Conventional Commits、分支模型、PR/MR、合并与冲突、SemVer/发布与 Git LFS | [01-Git协作与版本规范.md](./references/01-Git协作与版本规范.md) | — |
| README 基线、ADR 规范、API 文档、CHANGELOG 与文档新鲜度治理 | [02-文档治理与ADR规范.md](./references/02-文档治理与ADR规范.md) | [ADR_TEMPLATE.md](./assets/ADR_TEMPLATE.md) |

## 自动化治理校验 (`scripts/`)

提交、评审或维护 ADR/大资产时，可运行内置零依赖脚本校验 Conventional Commits、ADR 编号/章节与不可变性以及 `.gitattributes` Git LFS 配置：

```bash
python3 ./scripts/check_git_governance.py <repo_root> [--commit-msg "<msg>" | --check-adr --adr-base origin/main | --check-lfs]
```

## 跨 Skill 协作

- 行为红线与 L3/L4 计划审批见 `acs-core-workflow:references/01-核心铁律与五阶执行模型.md`。
- PR 代码审查流水线见 `acs-code-review:references/01-代码审查规范.md`。
- 机器可读 API 契约细则见 `acs-engineering-baseline:references/05-API与接口契约规范.md`。
