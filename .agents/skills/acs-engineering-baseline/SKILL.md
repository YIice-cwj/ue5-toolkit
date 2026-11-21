---
name: acs-engineering-baseline
description: >-
  Cross-language software engineering baseline: naming & documentation (B-CMT), architecture principles
  (SOLID/SRP/single-class-per-file/Expand-Contract), 95% coverage & anti-hollow testing, benchmarking,
  readability, API contracts (idempotency/cursor pagination), zero-downtime database migrations,
  observability/PII redaction, and application security (AuthN/AuthZ/XSS/CSRF/SSRF/crypto).
  Use for any backend, system, API, database, security, testing, or performance engineering task,
  and as the universal foundation for languages without dedicated domain skills (Go, Java, Rust, etc.).
  多语言通用代码规范、架构健壮性、95%真测试与性能基准、API/数据库/可观测性与应用安全基线。
license: MIT
metadata:
  domain: 10-通用代码规范
  version: "2.17.0"
  updated: "2026-11-20"
---

# ACS · Engineering Baseline (多语言通用工程基线)

本 Skill 封装 `10-通用代码规范` 全部 8 个专题及静态合规扫描工具，是多语言通用基准；对于 Go、Java、Rust、C# 等暂无专项 Skill 的技术栈，本 Skill 为主要技术遵循基线。

## 一票否决红线速查

- **注释与自解释 (B-CMT)**：函数体内零业务解释性行内注释；函数上方必须使用语言原生多行文档头，改动代码后 100% 同步更新。
- **单文件单类与职责边界 (`R-ARCH-CLASS`)**：每文件仅放一个核心类/聚合；UI/表现层严禁越权直接篡改核心业务数据。
- **95% 真测试契约与基准**：核心业务行/分支覆盖率 $\ge 95\%$；严禁无断言刷行、空洞断言、吞异常、全量 Mock 核心逻辑与 `sleep` 盲等；性能回归需在稳定环境、固定输入下对比基线。
- **API 与持久化**：写接口支持幂等键，大列表用游标分页；数据库演进坚持零停机 Expand-Contract，窄事务与参数化绑定防 SQL 注入与 N+1。
- **安全与隐私**：最小权限鉴权（防 BOLA/IDOR）、外部边界强校验、SSRF 防护、强加密；日志结构化并严格脱敏 PII/Token/Secret。

## 按主题加载详细规范 (`references/`)

| 任务主题 | 必读规范 |
|---|---|
| 通用风格、命名、意图常量与注释格式 (B-CMT) | [01-风格命名与注释.md](./references/01-风格命名与注释.md) |
| 工程七大守则、单文件单类、展开-收缩演进与错误处理 | [02-架构健壮性与工程安全.md](./references/02-架构健壮性与工程安全.md) |
| 95% 覆盖率门禁、真测试契约、Flaky 治理与性能基准测试 (§5 SSOT) | [03-测试控制流与速查.md](./references/03-测试控制流与速查.md) |
| 代码可读性、SLAP 单一抽象层次、心智负荷控制与排障友好 | [04-代码可读性与心智负荷.md](./references/04-代码可读性与心智负荷.md) |
| REST/RPC API 契约、幂等性、游标分页与版本向后兼容 | [05-API与接口契约规范.md](./references/05-API与接口契约规范.md) |
| 数据库零停机迁移、事务边界、索引与 N+1 防护 | [06-数据库与持久化演进规范.md](./references/06-数据库与持久化演进规范.md) |
| 生产级结构化日志、PII 隐私脱敏、Metrics/Tracing 与健康探针 | [07-可观测性与隐私规范.md](./references/07-可观测性与隐私规范.md) |
| 应用安全基线：认证授权、输入输出、XSS/CSRF/SSRF、加密与反序列化 | [08-应用安全规范.md](./references/08-应用安全规范.md) |

## 自动化合规与真测试校验 (`scripts/`)

修改源码或测试后，执行内置零依赖脚本核验高危一票否决项、反滥竽充数测试与 95% 覆盖率门禁：

```bash
python3 ./scripts/lint_rules.py <target_directory>
python3 ./scripts/check_test_quality.py <test_directory> [--cov-xml coverage.xml | --lcov lcov.info]
```

## 跨 Skill 优先级

当任务同时命中语言专有 Skill（`acs-cpp`、`acs-ue5`、`acs-web-frontend`、`acs-python`）时，语言专有约束优先于通用基准，行为协议 `acs-core-workflow` 最高优先。
