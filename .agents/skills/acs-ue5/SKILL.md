---
name: acs-ue5
description: >-
  Unreal Engine 5 (UE5.1+) C++ engineering standards: Epic naming (U/A/F/E/I/b prefixes, no _t suffix),
  UHT reflection macros (UCLASS/UPROPERTY/UFUNCTION), GC safety & TObjectPtr, UE containers & strings
  (no std::string/std::thread), Gameplay/GAS lifecycle & replication/RPC, async asset loading (TSoftObjectPtr),
  versioned SaveGame, UMG/Slate, Enhanced Input, Unreal Insights performance budgets, and C++/UE5 conflict arbitration.
  Use for any Unreal Engine 5 C++, Gameplay, UObject, Blueprint, UMG/Slate, or UE5 performance task.
  UE5 引擎 C++ 特化规范：反射宏、GC、网络同步、异步资源加载、SaveGame、UMG/Slate、Enhanced Input 与 Insights 性能预算。
license: MIT
metadata:
  domain: 30-UE5规范
  version: "3.7.0"
  updated: "2026-11-20"
---

# ACS · Unreal Engine 5 (UE5 引擎 C++ 特化规范)

本 Skill 封装 `30-UE5规范` 全部 11 篇文档，适用于 Unreal Engine 5.1+ 游戏与工具插件开发。本 Skill 在 `acs-cpp` 基础上派生并覆盖引擎特化规则。

## 一票否决红线速查

- **Epic 命名铁律**：强制 `U`/`A`/`F`/`E`/`I`/`S` 前缀与 PascalCase，布尔强制 `b` 前缀，**严禁 `_t` 后缀**；注释遵循多行分标签 Doxygen（`/**` 与 `*/` 独占行）。
- **UObject 与 GC 安全**：所有持有的 `UObject` 成员变量必须使用 `TObjectPtr<T>` 并标记 `UPROPERTY()` 防 GC 误杀；生命周期已保证时严禁层层 `IsValid()` 过度防御。
- **严禁绕过引擎生态**：严禁在 UE5 业务代码中使用 `std::string`、`std::vector`、`std::thread`/`std::jthread`，一律使用 `FString`/`FName`/`FText`、`TArray`/`TMap`、`FAsyncTask`/`Async`/`ParallelFor`。
- **网络与 Gameplay**：坚持三路职责分离（Authority / AutonomousProxy / SimulatedProxy）与生命周期自驱动；RPC 必须显式校验权限与参数。
- **资源加载与性能**：大资源默认 `TSoftObjectPtr` + `FStreamableManager` 异步加载，禁止主线程同步硬加载阻塞帧率；SaveGame 必须带版本号迁移；性能结论必须基于 Unreal Insights 采样与基线对照。

## 按主题加载详细规范 (`references/`)

| 涉及主题 | 必读规范 |
|---|---|
| 格式排版、Epic 命名体系与多行 Doxygen | [01-命名格式与注释.md](./references/01-命名格式与注释.md) |
| 接口优先类排布、`UINTERFACE` 与头文件依赖 | [02-类结构接口与头文件.md](./references/02-类结构接口与头文件.md) |
| 函数规范、禁用 `std` 映射表与移动语义 | [03-函数移动语义与现代特性.md](./references/03-函数移动语义与现代特性.md) |
| UObject 生命周期、GC 强引用铁律与智能指针选型 | [04-内存与GC.md](./references/04-内存与GC.md) |
| `UCLASS`/`UPROPERTY`/`UFUNCTION`/`USTRUCT` 反射与 Specifier 速查 | [05-反射与宏.md](./references/05-反射与宏.md) |
| UE 容器、三大字符串 (`FString`/`FName`/`FText`) 与并发线程安全 | [06-并发容器与安全.md](./references/06-并发容器与安全.md) |
| 断言体系 (`check`/`ensure`)、`UE_LOG`、自动化测试与模块构建 | [07-错误日志测试与构建.md](./references/07-错误日志测试与构建.md) |
| 网络复制、RPC、GAS 与 Gameplay 生命周期、三路职责分离 | [08-网络同步与Gameplay生命周期.md](./references/08-网络同步与Gameplay生命周期.md) |
| 委托选型、容器选型与 **C++ / UE5 冲突仲裁表（附录 C）** | [09-速查与仲裁附录.md](./references/09-速查与仲裁附录.md) |
| 软引用异步加载、SaveGame 版本化序列化与 Insights 性能预算 | [10-资源加载序列化与性能规范.md](./references/10-资源加载序列化与性能规范.md) |
| UMG/Slate UI、Enhanced Input、模块插件化与本地化 | [11-UMG_Slate输入与工程化规范.md](./references/11-UMG_Slate输入与工程化规范.md) |

## 自动化专项检查 (`scripts/`)

修改 UE5 C++ 代码后，运行内置零依赖脚本跨行检查 `UPROPERTY()` 与 GC 强引用配对、`*.generated.h` 顺序、Epic 前缀、禁用 `std::` 与主线程同步硬加载：

```bash
python3 ./scripts/check_ue5_standards.py <ue5_source_directory>
```

## 跨 Skill 依赖与仲裁

- 本 Skill 继承 `acs-cpp` 的基础语言规范与 `acs-engineering-baseline` 的通用工程契约。
- 当 `acs-ue5` 与 `acs-cpp` 规则冲突时，**`acs-ue5` 绝对优先**，逐项裁定见 [09-速查与仲裁附录.md](./references/09-速查与仲裁附录.md#附录-cc-规范与-ue5-冲突仲裁表)。
