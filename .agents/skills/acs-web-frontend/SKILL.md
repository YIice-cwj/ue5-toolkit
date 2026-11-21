---
name: acs-web-frontend
description: >-
  Modern Web frontend engineering standards for TypeScript, React, and Next.js: strict TypeScript (no any,
  unknown + type narrowing), multi-line JSDoc, React Hooks exhaustive deps & closure safety, state colocation,
  Server/Client boundary & SSR hydration safety, frontend testing (Vitest/RTL/Playwright), Web security
  (XSS/CSRF/CSP), WCAG 2.1 AA accessibility (a11y), and Core Web Vitals / Lighthouse CI budgets.
  Use when building, refactoring, auditing, or reviewing TS/TSX, React, Next.js, or frontend performance/a11y code.
  现代 Web 前端规范：TypeScript 严格类型、React Hooks/SSR 水合安全、前端测试、Web 安全、无障碍与 Core Web Vitals。
license: MIT
metadata:
  domain: 60-Web前端规范
  version: "1.3.0"
  updated: "2026-11-20"
---

# ACS · Web Frontend (现代 Web 前端工程规范)

本 Skill 封装 `60-Web前端规范` 全部 3 篇文档，适用于 TypeScript、React、Next.js 及现代 Web 应用开发与性能优化。

## 一票否决红线速查

- **零 `any` 与类型收窄**：严禁 `: any`、`as any`、`<any>`；外部不确定数据使用 `unknown` + 类型守卫或 Schema 校验（如 Zod）收缩。
- **多行 JSDoc 与零行内注释 (B-CMT)**：函数/组件上方使用多行 JSDoc，函数体内严禁业务解释性行内注释；改动代码后同步更新 JSDoc。
- **React 架构与水合安全**：Hooks 依赖数组必须完整（禁 `eslint-disable` 屏蔽依赖告警）；状态下沉避免全局无关重渲染；严格隔离 Server/Client 组件边界，杜绝 SSR/CSR 渲染结果不一致引发 Hydration Mismatch。
- **前端安全与无障碍**：严禁 `eval`、`new Function` 与未经净化的 `dangerouslySetInnerHTML`；落实 CSP 与 CSRF 防护；交互控件满足 WCAG 2.1 AA（语义标签、键盘可达、对比度）。
- **测试与 Core Web Vitals**：交互断言使用 Testing Library / Playwright（严禁 `setTimeout` 盲等）；核心页面通过 Lighthouse CI 守护 CWV 75 分位预算。

## 按主题加载详细规范 (`references/`)

| 涉及主题 | 必读规范 |
|---|---|
| TypeScript 强类型约束、类型窄化、JSDoc 与语法红线 | [01-TypeScript与基础规范.md](./references/01-TypeScript与基础规范.md) |
| React Hooks 闭包防御、状态下沉、Server/Client 边界与 SSR 水合安全 | [02-React与现代前端架构.md](./references/02-React与现代前端架构.md) |
| 构建与依赖治理、测试策略、Web 安全 (XSS/CSRF/CSP)、WCAG 2.1 AA 与 Core Web Vitals | [03-前端工程化测试与安全规范.md](./references/03-前端工程化测试与安全规范.md) |

## 自动化专项检查 (`scripts/`)

修改 TypeScript / React / Next.js 源码后，运行内置零依赖脚本核验 `any`/`@ts-ignore` 逃逸、Hooks 依赖禁用、未净化 HTML、SSR 水合高危访问与基础 `<img>` `alt` 无障碍：

```bash
python3 ./scripts/check_frontend_standards.py <src_directory_or_file>
```

## 跨 Skill 协作

- 通用应用安全与 API 契约见 `acs-engineering-baseline:references/08-应用安全规范.md` 与 `acs-engineering-baseline:references/05-API与接口契约规范.md`。
- 95% 覆盖率门禁与性能回归总则见 `acs-engineering-baseline:references/03-测试控制流与速查.md`。
- 注：Vue 等其他前端框架可参考本 Skill 的 TypeScript / 安全 / a11y / CWV 章节与 `acs-engineering-baseline`，不套用 React 专有条款。
