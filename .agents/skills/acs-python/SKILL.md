---
name: acs-python
description: >-
  Modern Python (3.10+/3.11+) engineering standards: strict type annotations, multi-line Docstrings,
  no mutable default args or bare except, Pydantic v2 & pydantic-settings, pyproject.toml (ruff/mypy),
  asyncio non-blocking I/O with timeouts & cancellation propagation, locked deps (uv/Poetry),
  SQLAlchemy/Alembic migrations & service boundaries, pytest >=95% branch coverage (no sleep),
  and pytest-benchmark performance regression checks.
  Use when writing, refactoring, debugging, or reviewing Python code, async services, or pytest suites.
  Python 工程规范：严格类型、Pydantic v2、asyncio 并发与超时取消、数据访问/Alembic、pytest 95%覆盖率与基准测试。
license: MIT
metadata:
  domain: 70-Python工程规范
  version: "1.3.0"
  updated: "2026-11-20"
---

# ACS · Python Engineering (Python 工程规范)

本 Skill 封装 `70-Python工程规范` 全部 3 篇文档，适用于 Python 后端服务、数据管道、CLI 与自动化工具工程。

## 一票否决红线速查

- **类型标注与多行 Docstring (B-CMT)**：公开函数与方法必须标注完整形参/返回类型（`pyproject.toml` 统一管理 `ruff` + `mypy`/`pyright`）；使用多行 Docstring 且函数体内零业务行内注释。
- **禁致命语法陷阱**：严禁可变默认实参（`def f(x=[])` / `x={}`）；严禁裸 `except:` 吞异常；严禁对不可信输入使用 `eval`/`exec`/`pickle`；I/O 与锁 100% 使用 `with`/`async with`。
- **强契约与配置校验**：外部边界数据采用 Pydantic v2 或 `dataclass(slots=True)` 校验；环境配置用 `pydantic-settings` 启动即校验，严禁明文硬编码密钥。
- **asyncio 并发安全**：异步事件循环中严禁直接执行同步阻塞 I/O（必须 `asyncio.to_thread` 或异步驱动）；所有外部调用必设超时；严禁吞掉 `asyncio.CancelledError`。
- **依赖、数据与测试**：依赖使用 `uv`/`Poetry` 锁死并提交 Lockfile；数据库变更遵循 Alembic 可回滚迁移与窄事务；核心业务 `pytest --cov --cov-branch --cov-fail-under=95`，严禁 `time.sleep` 盲等；性能路径使用 `pytest-benchmark` 做相对回归。

## 按主题加载详细规范 (`references/`)

| 涉及主题 | 必读规范 |
|---|---|
| 风格、严格类型、Pydantic v2、`pyproject.toml` 与基础安全 | [01-风格类型与现代特性.md](./references/01-风格类型与现代特性.md) |
| `asyncio` 非阻塞并发、超时与取消、后台任务幂等、`pytest` 95% 覆盖率与 `pytest-benchmark` | [02-并发工程与测试规范.md](./references/02-并发工程与测试规范.md) |
| 项目结构、依赖锁、服务边界、SQLAlchemy 数据访问与 Alembic 迁移 | [03-工程化与数据访问规范.md](./references/03-工程化与数据访问规范.md) |

## 自动化 AST 深度检查 (`scripts/`)

修改 Python 源码后，运行内置零依赖 `ast` 检查脚本核验公开类型标注、多行 Docstring、函数体零行内业务注释 (`B-CMT`)、可变默认实参、裸 `except` 与 `asyncio` 阻塞/吞取消：

```bash
python3 ./scripts/check_python_ast.py <target_directory_or_file>
```

## 跨 Skill 协作

- 通用 API、零停机数据库演进、可观测性与应用安全基线见 `acs-engineering-baseline:references/05-API与接口契约规范.md` 至 `acs-engineering-baseline:references/08-应用安全规范.md`。
- 95% 真测试契约与性能回归门禁 SSOT 见 `acs-engineering-baseline:references/03-测试控制流与速查.md`。
