# GameDebug 快速上手与最佳实践指南

## 1. 模块接入

在目标插件或游戏模块的 `.Build.cs` 中按需添加依赖：

```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "GameDebug",
});
```

在 `.uplugin` 或 `.uproject` 中启用 `GameDebug`（依赖 `GameCore` 与 `GameLogging`）。

---

## 2. 典型使用示例

### 2.1 注册与执行模块化 GM 指令

```cpp
#include "Cheat/FGameCheatRegistry.h"

void RegisterInventoryCheats() {
    FGameCheatRegistry::Get().RegisterCheat(
        FName(TEXT("GM.GiveItem")),
        TEXT("发放测试道具。用法: GM.GiveItem \"Item Name\" <Count>"),
        [](const TArray<FString>& Args) {
            if (Args.Num() < 2) {
                return;
            }
            const FString& ItemName = Args[0];
            int32 Count = 1;
            LexTryParseString(Count, *Args[1]);
            // 调用业务背包发奖接口
        }
    );
}
```

### 2.2 通道原地刷新打印与实时变量监控

```cpp
#include "Print/FGameDebugPrint.h"
#include "Watcher/FGameDebugWatchHub.h"

void AMyCharacter::BeginPlay() {
    Super::BeginPlay();

    TWeakObjectPtr<AMyCharacter> WeakSelf(this);
    GAME_DEBUG_WATCH("Player", "Speed", [WeakSelf]() {
        if (const AMyCharacter* Self = WeakSelf.Get()) {
            return FString::Printf(TEXT("%.1f cm/s"), Self->GetVelocity().Size());
        }
        return FString(TEXT("Destroyed"));
    });
}

void AMyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    GAME_DEBUG_UNWATCH("Player", "Speed");
    Super::EndPlay(EndPlayReason);
}

void AMyCharacter::OnTakeDamage(float Amount) {
    GAME_DEBUG_PRINT_CHANNEL(FName(TEXT("Player")), "Taken Damage: %.1f", Amount);
}
```

> 同名通道 `"Player"` 与监控分组 `"Player"` 使用正交域盐键，在屏幕上互不覆盖。

### 2.3 弹道轨迹可视化与作用域耗时告警

```cpp
#include "Profiling/FScopedDebugStopwatch.h"
#include "Visualizer/FGameDebugTrailRecorder.h"

void AMyProjectile::Tick(float DeltaSeconds) {
    GAME_DEBUG_SCOPED_TIMER_CHANNEL("ProjectileTick", 0.5, "CombatPerf");
    Super::Tick(DeltaSeconds);

    TrailRecorder.AddSample(GetActorLocation());
    TrailRecorder.DrawTrail(GetWorld(), FColor::Green, FColor::Blue, 2.0f, 0.0f);
}
```

### 2.4 常用控制台调试指令速查

| 指令 | 说明 |
|---|---|
| `GM.Watch list` | 列出全局开关与全部已注册观察分类状态 |
| `GM.Watch all off` / `GM.Watch all on` | 一键开关全部屏幕观察面板 |
| `GM.Watch Player off` | 关闭指定分类面板（支持 `on/off/1/0/true/false`） |
| `GM.TimeScale 0.2` | 设置全局慢动作倍率（范围 `0.001 ~ 20.0`） |
| `GM.TimePause` / `GM.TimeResume` | 冻结世界时钟 / 恢复暂停前记忆倍率 |
| `GM.TimeStep 1` | 冻结态下逐帧单步推进 N 帧后自动重暂停 |
| `GM.TimeReset` | 强制重置世界时钟与记忆倍率为 `1.0x` |

---

## 3. 最佳实践与注意事项

1. **防悬空捕获**：向 `FGameDebugWatchHub::Watch` 注册的闭包捕获 `UObject`/`AActor` 时，务必使用 `TWeakObjectPtr<T>` 并在 `EndPlay` / 析构时对称调用 `Unwatch`。
2. **结构化日志与屏幕打印职责分离**：需要落盘或远程上报的诊断日志使用 `GameLogging`（`GAME_LOG`）；需要即时在游戏视口原地刷新的高频数值使用 `FGameDebugPrint` / `FGameDebugWatchHub`。
3. **Shipping 零开销**：优先使用 `GAME_DEBUG_*` 宏（在 Shipping 下连同实参求值一起编译期擦除）；直接调用 C++ API 也会在函数入口经 `if constexpr` 零开销短路。
