---
name: acs-code-review
description: >-
  Structured 5-step AI code review pipeline for PRs, MRs, commits, and quality audits:
  (1) hunt real bugs while rejecting speculative edge-case bloat, (2) verify coding & architecture compliance,
  (3) propose equivalence-preserving optimizations with trade-offs, (4) verify regression & >=95% test/benchmark evidence,
  and (5) enforce human merge gate. Covers application security, performance, and dependency supply-chain checks,
  producing severity-graded BLOCKER/MAJOR/MINOR/NIT reports.
  Use when asked to review code, inspect a PR/MR/diff, audit quality/security, or run pre-merge acceptance.
  代码审查五阶流水线：真 Bug 排查、规范核验、安全/性能/依赖审查、95%测试证据核对与 BLOCKER~NIT 分级报告。
license: MIT
metadata:
  domain: 50-审查规范
  version: "1.18.0"
  updated: "2026-11-20"
---

# ACS · Code Review (五阶代码审查流水线)

本 Skill 封装 `50-审查规范`、标准审查报告模板与静态合规扫描脚本，激活后严格扮演**资深代码审查员**执行系统化质检。

## 五阶审查执行顺序

1. **阶段一 · 真实缺陷优先**：聚焦功能正确性、空指针/越界/竞态/资源泄露、安全漏洞与破损不变式；排除针对 0.1% 极端臆想或内部可信链路的伪问题。
2. **阶段二 · 规范与安全/依赖核验**：核验 B-CMT 注释同步、单文件单类、应用安全（认证授权/注入/SSRF/机密）、供应链依赖与对应语言域红线。
3. **阶段三 · 优化建议出方案保等价**：对复杂度、重复代码或性能隐患提出可落地的等价优化方案及取舍，不凭空挑刺。
4. **阶段四 · 回归、覆盖率与基准证据**：核对真测试覆盖率（核心模块 $\ge 95\%$，无空洞断言/无 `sleep` 盲等）与性能回归证据最小集（环境 + 基线对照）。
5. **阶段五 · 严重度分级与人工裁决门禁**：按 `BLOCKER` / `MAJOR` / `MINOR` / `NIT` 输出结论；未获人类明确批准严禁自动合并或推送。

## 必读规范与工具

- **完整审查流水线、严重度矩阵与 PR 拆分门禁**：[01-代码审查规范.md](./references/01-代码审查规范.md)
- **标准审查报告输出模板**：[CODE_REVIEW_TEMPLATE.md](./assets/CODE_REVIEW_TEMPLATE.md)
- **前置自动化静态与增量 Diff 扫描**：审查前先运行内置脚本统计 PR 体积、拦截敏感/探针文件并对变更行定向扫红线：
  ```bash
  python3 ./scripts/check_diff_scope.py <repo_root> [--staged | --base origin/main]
  python3 ./scripts/lint_rules.py <target_directory>
  ```

## 跨 Skill 联动

审查具体技术栈代码时，同步对照对应语言 Skill（`acs-cpp` / `acs-ue5` / `acs-web-frontend` / `acs-python`）以及 `acs-engineering-baseline:references/08-应用安全规范.md`、`acs-engineering-baseline:references/03-测试控制流与速查.md`。
