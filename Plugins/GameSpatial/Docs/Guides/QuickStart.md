# GameSpatial 快速上手与最佳实践指南

---

## 一、 接入模块依赖

在业务模块的 `*.Build.cs` 中添加 `"GameCore"` 与 `"GameSpatial"`：

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "GameCore",
    "GameSpatial",
});
```

可直接包含全量聚合门面头 `#include "GameSpatial.h"`，或按需包含子域头文件（如 `#include "Grid/TSpatialHashGrid3D.h"`、`#include "Subsystem/UGameSpatialSubsystem.h"`、`#include "Throttle/FSpatialThrottleDriver.h"`）。

---

## 二、 典型 C++ 用法

### 1. 纯逻辑 2D/3D 空间网格与零分配流式查询
```cpp
#include "Grid/TSpatialHashGrid3D.h"

void FCombatTargeting::QuerySkillTargets(const TSpatialHashGrid3D<int32>& Grid, const FVector& CastPos, const FVector& AimDir) {
    // 1. 零堆分配流式锥体命中判定（带紧致 3D 锥体包围盒剪枝与无开方角判别）
    Grid.QueryConeMatching(
        CastPos,
        AimDir,
        1200.0f,
        30.0f,
        [](int32 TargetId, const FVector& Loc) {
            ApplyConeDamage(TargetId, Loc);
        },
        [](int32 TargetId) {
            return IsHostileTarget(TargetId);
        }
    );

    // 2. K 近邻链式弹跳锁定（K <= 16 全程栈内联零堆分配，严格按距离升序）
    TArray<int32> BounceTargets;
    Grid.QueryKNearest(CastPos, 2000.0f, 5, BounceTargets, [](int32 TargetId) {
        return IsAliveTarget(TargetId);
    });
}
```

### 2. 世界级 Actor 分层空间索引子系统
```cpp
#include "Subsystem/UGameSpatialSubsystem.h"

void AEnemyActor::BeginPlay() {
    Super::BeginPlay();
    if (UGameSpatialSubsystem* Spatial = GetWorld()->GetSubsystem<UGameSpatialSubsystem>()) {
        Spatial->RegisterActor(this, EnemyLayerTag);
    }
}

void AEnemyActor::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (UGameSpatialSubsystem* Spatial = GetWorld()->GetSubsystem<UGameSpatialSubsystem>()) {
        Spatial->UnregisterActor(this);
    }
    Super::EndPlay(EndPlayReason);
}

void APlayerCharacter::PerformRadialPulse() {
    if (const UGameSpatialSubsystem* Spatial = GetWorld()->GetSubsystem<UGameSpatialSubsystem>()) {
        // C++ 零分配流式查询敌对图层（自动剔除已销毁弱引用）
        Spatial->ForEachActorInRadius(GetActorLocation(), 800.0f, [this](AActor* Enemy, const FVector& EnemyLoc) {
            ApplyPulseImpulse(Enemy, EnemyLoc);
        }, EnemyLayerTag);
    }
}
```

### 3. 视锥重要度心跳节流驱动器
```cpp
#include "Async/UThrottledTickerSubsystem.h"
#include "Throttle/FSpatialThrottleDriver.h"

void UAiLodController::Initialize(UWorld* World) {
    UThrottledTickerSubsystem* Ticker = World->GetSubsystem<UThrottledTickerSubsystem>();
    ThrottleDriver = MakeUnique<FSpatialThrottleDriver>(*Ticker);

    FSpatialThrottleTierConfig Config;
    Config.DistanceNear = 1500.0f;
    Config.DistanceMid = 3500.0f;
    Config.DistanceFar = 7000.0f;
    Config.DistanceHysteresis = 80.0f;
    Config.HysteresisDegrees = 5.0f;
    ThrottleDriver->SetTierConfig(Config);
}
```

---

## 三、 工程最佳实践与性能建议

1. **单元格边长选型**：`CellSize` 建议设为高频查询半径的 `1.0× ~ 2.0×`（默认 `500.0f` 适用于大多数 3D ARPG/FPS 战斗半径）；批量导入前可调用 `Reserve(ExpectedCount)` 消除早期扩容。
2. **优先使用流式查询原语**：高频每帧查询优先使用 `Query*Matching` 与 `UGameSpatialSubsystem::ForEachActorIn*`，彻底省去中间 `TArray` 堆分配。
3. **成对注册/注销并定期清理**：推荐在 `Actor::EndPlay` 主动调用 `UnregisterActor(this)`；对可能非正常销毁的动态 Actor，可在切区或低频定时器中调用 `PruneAllStaleActors()` 回收惰性弱引用条目。
4. **本地自动化测试与微基准回归**：
   ```bash
   python3 .scripts/run_automation_tests.py --case Game.Spatial
   ```
