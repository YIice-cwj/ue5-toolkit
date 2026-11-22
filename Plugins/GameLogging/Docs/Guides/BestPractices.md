# GameLogging 最佳实践与性能指南

## 1. 警惕高频 Tick 日志，优先使用 `GAME_LOG_THROTTLE`

### ❌ 错误做法：Tick 中直接使用 `GAME_LOG`
```cpp
void AProjectile::Tick(float DeltaTime) {
    Super::Tick(DeltaTime);
    GAME_LOG(Verbose, TEXT("Velocity: %s"), *GetVelocity().ToString());
}
```

### ✅ 正确做法：使用 `GAME_LOG_THROTTLE` 限频
```cpp
void AProjectile::Tick(float DeltaTime) {
    Super::Tick(DeltaTime);
    // 抑制态零时钟/零 CAS 开销；开启 Verbose 后首帧即打，后续每 1 秒至多输出一次
    GAME_LOG_THROTTLE(1.0, Verbose, TEXT("Velocity: %s"), *GetVelocity().ToString());
}
```

---

## 2. 纯静态优化：极致热点指定 `GAME_CURRENT_MODULE_LOG`

常驻槽位动态分发在热路径仅需 2 次内联原子读（约 1~2ns）。若需在极端高频内循环彻底静态直连原生 `UE_LOG`，可在独立 `.cpp` 顶部显式定义（注意在 Unity Build 环境下应避免跨文件污染，确保在包含任何日志头前定义并在文件尾 `#undef`）：

```cpp
#define GAME_CURRENT_MODULE_LOG LogMyModule
#include "Logging/GameLogMacros.h"
```

---

## 3. Shipping 包零开销控制

`GameLogging` 与 `GameCore/Public/Config/GameCompileSwitches.h` 联动：

```cpp
#ifndef GAME_ENABLE_LOG
    #define GAME_ENABLE_LOG ((NO_LOGGING) == 0)
#endif
```

在 Shipping 构建中 `NO_LOGGING` 默认为 `1`，`Game::Config::bEnableLog` 自动为 `false`，所有 `GAME_LOG*` 分支由 `if constexpr` 静态裁剪。可通过 `Target.cs` 显式覆写：

```csharp
GlobalDefinitions.Add("GAME_ENABLE_LOG=0");
```

---

## 4. 远程日志收集与网络上报最佳实践

### 4.1 保持默认高级别过滤阈值
远程日志默认最低采集级别为 `ELogVerbosity::Warning`，严禁在生产环境长期降为 `Log` / `Verbose`。联调时通过子系统门面动态调整：

```cpp
if (UGameLoggingSubsystem* Subsystem = UGameLoggingSubsystem::GetPtr(this)) {
    Subsystem->SetMinVerbosity(ELogVerbosity::Log);
}
```

### 4.2 使用子系统门面配置黑名单压制噪音
设备已默认忽略 `LogGameMessage`、`LogGameNetService` 与 `LogGameNetCore`。如需压制第三方噪音分类，直接调用子系统统一门面：

```cpp
if (UGameLoggingSubsystem* Subsystem = UGameLoggingSubsystem::GetPtr(this)) {
    Subsystem->AddIgnoredCategory(FName(TEXT("LogThirdPartySDK")));
}
```

### 4.3 零耦合单向监听，严禁反向引用
`GameLogging` 不依赖任何网络模块或 `GamePool`。外部网络或遥测插件只需包含 `Remote/GameRemoteLogTypes.h` 并通过 `UGameplayMessageSubsystem` 订阅 `TAG_Game_Log_Remote` 即可。

---

## 5. 常见问题排查 (Troubleshooting)

### Q1: 为什么我的日志都输出到了 `LogGame` 而不是自定义的模块名？
- **原因**：当前模块尚未调用 `GAME_REGISTER_MODULE_LOG(LogYourModule)`。
- **解决**：在模块主 cpp（如 `YourModule.cpp`）顶层加入 `GAME_REGISTER_MODULE_LOG(LogYourModule);`。

### Q2: 出现模块名大小写识别问题？
- **机制**：注册表采用区分大小写的字节精确比较（`FMemory::Memcmp`），模块名必须与 UBT 注入的 `UE_MODULE_NAME` 大小写完全一致。
