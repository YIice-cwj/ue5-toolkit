---
name: acs-cpp
description: >-
  Modern C++ (C++20/23, C++17 compatible) engineering standards: naming (_t suffix) & multi-line Doxygen,
  interface-first 15-layer class layout, single-class-per-file, templates & concepts, NRVO (no return std::move),
  RAII smart pointers, concurrency & error handling, Modern CMake, vcpkg/Conan, GoogleTest/Catch2,
  Sanitizers, llvm-cov/gcov coverage, ABI stability, and Google Benchmark regression checks.
  Use when authoring, refactoring, debugging, or reviewing C++ (.h/.hpp/.cc/.cpp), CMakeLists.txt, or C++ benchmarks.
  For Unreal Engine 5 C++, load BOTH acs-cpp and acs-ue5 (acs-ue5 overrides conflicts).
  现代 C++ 编码规范：15层类布局、NRVO、RAII、模板/concept、并发、Modern CMake、Sanitizer 与基准测试。
license: MIT
metadata:
  domain: 20-CPP规范
  version: "6.13.0"
  updated: "2026-11-20"
---

# ACS · Modern C++ (现代 C++ 工程规范)

本 Skill 封装 `20-CPP规范` 全部 7 篇专项文档，适用于 C++20/C++23（最低兼容 C++17）项目开发、重构与性能验证。

## 一票否决红线速查

- **多行分标签 Doxygen (B-CMT)**：`/**` 与 `*/` 各自独占行，`@brief` 必填，`@tparam/@param/@return` 逐项成行；**严禁单行压缩** `/** @brief ... */`；函数体内零业务行内注释。
- **接口优先 15 层类布局**：声明严格按 15 层自顶向下排布，`.cc`/`.cpp` 实现顺序与头文件声明 1:1 对齐；坚持单文件单类。
- **禁阻断 NRVO 与过度防御**：严禁 `return std::move(local_var)`；严禁 `&ref == nullptr` 或 `this == nullptr`；严禁裸 `catch(...)` 吞异常。
- **RAII 与资源安全**：优先值语义与 `std::unique_ptr`，零裸 `new`/`delete`；严禁裸指针表达所有权。
- **构建、测试与基准**：Modern CMake target-based 依赖管理；启用 ASan/UBSan/TSan 验证；核心逻辑覆盖率 $\ge 95\%$；微基准使用 Google Benchmark / Catch2 Benchmark 固定输入对照基线。

## 按主题加载详细规范 (`references/`)

命中 C++ 任务时，先读入口 `01` 并按实际涉及主题加载下列全部相关分节：

| 涉及主题 | 必读规范 |
|---|---|
| 格式排版、头文件顺序、命名 (`_t` 等) 与多行 Doxygen | [01-格式命名与注释.md](./references/01-格式命名与注释.md) |
| 15 层类布局、单文件单类、函数设计与必要防御 | [02-类结构与函数设计.md](./references/02-类结构与函数设计.md) |
| C++20/23 现代特性、`concept`、模板与泛型约束 | [03-现代特性与模板.md](./references/03-现代特性与模板.md) |
| 构造/移动语义、NRVO、RAII 智能指针与内存安全 | [04-生命周期与内存安全.md](./references/04-生命周期与内存安全.md) |
| 错误处理、日志、并发线程安全与宏禁令 | [05-并发错误处理与工程规范.md](./references/05-并发错误处理与工程规范.md) |
| 容器选择决策速查与高频自检对照表 | [06-速查附录.md](./references/06-速查附录.md) |
| Modern CMake、vcpkg/Conan、测试覆盖率、Sanitizer、性能基准、ABI 与序列化 | [07-构建测试与性能规范.md](./references/07-构建测试与性能规范.md) |

## 自动化专项检查 (`scripts/`)

提交或审查现代 C++ 代码前，运行内置零依赖检查脚本核验头文件守卫、多行分标签 Doxygen、单文件单类、裸 `new`/`delete` 与跨行 NRVO：

```bash
python3 ./scripts/check_cpp_standards.py <target_directory_or_file>
```

## 跨 Skill 协作与冲突仲裁

- 通用架构、95% 覆盖率与性能基准总则见 `acs-engineering-baseline:references/03-测试控制流与速查.md`。
- **UE5 C++ 项目**：必须同时加载 `acs-ue5`；命名（如禁 `_t` 后缀、布尔 `b` 前缀）、字符串、容器与智能指针等冲突项一律以 `acs-ue5:references/09-速查与仲裁附录.md`（附录 C）为准。
