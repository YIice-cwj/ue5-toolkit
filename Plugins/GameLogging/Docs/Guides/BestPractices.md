# GameLogging 最佳实践与性能指南

## 1. 警惕高频 Tick 日志，强制使用 `GAME_LOG_THROTTLE`

### ❌ 错误做法：Tick 中直接使用 `GAME_LOG`
```cpp
void AProjectile::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // 危险：成百上千个弹道每帧写日志，导致 I/O 拥塞甚至卡顿丢帧！
    GAME_LOG(Verbose, TEXT("Velocity: %s"), *GetVelocity().ToString());
}
```

### ✅ 正确做法：使用 `GAME_LOG_THROTTLE` 限频
```cpp
void AProjectile::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // 安全：每 1 秒最多仅输出一次该弹道当前速度
    GAME_LOG_THROTTLE(1.0, Verbose, TEXT("Velocity: %s"), *GetVelocity().ToString());
}
```

---

## 2. 纯静态优化：关键高频热点指定 `GAME_CURRENT_MODULE_LOG`

动态分发通过原子操作读取局部缓存，开销仅为 1~2ns。但在物理每子步或每帧数万次调用的核心内循环中，如果追求完全的静态内联编译，可在该源文件最上方显式定义：

```cpp
// 在该 .cpp 的首行 include 之前定义
#define GAME_CURRENT_MODULE_LOG LogMyModule
#include "Logging/GameLogCategories.h"

void HeavyMathLoop()
{
    // 此时宏直接展开为 UE_LOG(LogMyModule, ...)，零任何原子读指令与动态分发
    GAME_LOG(VeryVerbose, TEXT("Iteration count..."));
}
```

---

## 3. Shipping 包零开销控制

`GameLogging` 与 `GameCore/Public/Config/GameCompileSwitches.h` 紧密结合：

```cpp
#ifndef GAME_ENABLE_LOG
    #define GAME_ENABLE_LOG ((NO_LOGGING) == 0)
#endif
```

在 Shipping 构建中，引擎默认将 `NO_LOGGING` 设为 `1`，从而 `bEnableLog` 自动为 `false`。
通过 C++20 `if constexpr` 分支判定，所有 `GAME_LOG`、`GAME_LOG_THROTTLE` 调用会被编译器前端全部擦除，不残留字符串与任何指令。

如果项目需要在 Shipping 构建中彻底移除或开启日志支持，可通过 Target.cs 或 Build.cs 统一修改预编译宏定义：
```csharp
// 在 Target.cs 中
GlobalDefinitions.Add("GAME_ENABLE_LOG=0"); // 强制彻底关闭所有日志宏
```

---

## 4. 常见问题排查 (Troubleshooting)

### Q1: 为什么我的日志都输出到了 `LogGame` 而不是自定义的模块名？
- **原因**：当前模块尚未调用 `GAME_REGISTER_MODULE_LOG(LogYourModule)`。
- **解决**：在模块主 cpp（如 `YourModule.cpp`）中加入 `GAME_REGISTER_MODULE_LOG(LogYourModule)` 即可。

### Q2: 出现模块名大小写或跨平台识别问题？
- **机制**：注册表比较采用 `FCStringAnsi::Strcmp` 区分大小写的精确匹配。UBT 传入的 `UE_MODULE_NAME` 与注册时的宏参数必须一致。

