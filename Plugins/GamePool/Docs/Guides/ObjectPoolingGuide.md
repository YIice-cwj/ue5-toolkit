# GamePool 对象与内存池实战指南

本指南详细介绍如何让自定义 Actor、纯 UObject 或原生 C++ 结构体接入 `UObjectPoolSubsystem` 与 `TStructPool`，实现零野指针、高吞吐复用与平滑休眠唤醒。

---

## 1. Actor 实现 `IGamePoolable` 接口

在您的 Actor 头文件中继承 `IGamePoolable`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/IGamePoolable.h"
#include "MyProjectile.generated.h"

UCLASS()
class AMyProjectile : public AActor, public IGamePoolable {
    GENERATED_BODY()

public:
    AMyProjectile();

    // 接口实现
    virtual void OnAcquireFromPool_Implementation() override;
    virtual void OnReleaseToPool_Implementation() override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProjectileMovementComponent> MovementComp;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USphereComponent> CollisionComp;
};
```

---

## 2. 编写重置与激活逻辑

在 Actor 的实现文件 `.cpp` 中编写状态重置代码：

```cpp
#include "MyProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SphereComponent.h"

AMyProjectile::AMyProjectile()
{
    PrimaryActorTick.bCanEverTick = true;
    CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
    RootComponent = CollisionComp;

    MovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MovementComp"));
}

void AMyProjectile::OnAcquireFromPool_Implementation()
{
    MovementComp->SetUpdatedComponent(RootComponent);
    MovementComp->Velocity = GetActorForwardVector() * MovementComp->InitialSpeed;
    MovementComp->Activate(true);

    CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AMyProjectile::OnReleaseToPool_Implementation()
{
    MovementComp->StopMovementImmediately();
    MovementComp->Deactivate();

    CollisionComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
```

---

## 3. 在发射器中借还（三种借出模式对比）

### 模式 A：RAII 作用域租约（强烈推荐用于短生命周期对象）
```cpp
#include "Subsystem/ObjectPoolSubsystem.h"
#include "MyProjectile.h"

void AMyWeapon::SpawnTransientHitBox()
{
    UWorld* World = GetWorld();
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(World);

    // 离开函数作用域时自动归还回池，杜绝泄漏
    auto Rental = Pool.AcquireScopedActor<AMyProjectile>(AMyProjectile::StaticClass(), GetActorTransform());
    if (Rental) {
        Rental->PerformDetection();
    }
}
```

### 模式 B：安全代数句柄（推荐用于跨帧、异步回调持有的对象）
```cpp
void AMyWeapon::FireAsync()
{
    UWorld* World = GetWorld();
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(World);

    auto HandleRes = Pool.AcquireActorHandle<AMyProjectile>(AMyProjectile::StaticClass(), GetMuzzleTransform());
    if (HandleRes.IsSuccess()) {
        TPooledObjectHandle<AMyProjectile> BulletHandle = HandleRes.GetValue();
        
        // 即使传递到 Lambda 闭包中，若子弹已被碰撞回调归还，句柄自动失效（Get() == nullptr）
        GetWorldTimerManager().SetTimer(TimerHandle, [BulletHandle, &Pool]() {
            if (BulletHandle.IsValid()) {
                Pool.ReleaseActor(BulletHandle);
            }
        }, 3.0f, false);
    }
}
```

### 模式 C：经典裸指针借出（兼容老项目与蓝图门面）
```cpp
void AMyWeapon::FireLegacy()
{
    UWorld* World = GetWorld();
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(World);

    AMyProjectile* Bullet = Pool.AcquireActor<AMyProjectile>(AMyProjectile::StaticClass(), GetMuzzleTransform()).GetValue();
    // 归还
    Pool.ReleaseActor(Bullet);
}
```

---

## 4. 软引用异步流式预热（防关卡加载卡顿）

在大世界地图切换或武器切换时，使用 C++20 协程进行异步流式加载与预热：

```cpp
TGameCoroTask<void> AMyWeapon::PrewarmAmmoAsync(FSoftObjectPath AmmoClassPath)
{
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);
    
    // 后台流式加载资产 -> 按帧时间预算平滑生成 20 发子弹 -> 支持取消令牌打断
    int32 Prewarmed = co_await Pool.PrewarmAsync(AmmoClassPath, 20, 2);
    UE_LOG(LogTemp, Log, TEXT("预热完成，生成 %d 发子弹"), Prewarmed);
}
```

---

## 5. 原生 C++ 结构体与高频数据内存池

针对网络数据包反序列化、战斗伤害结算上下文、空间射线查询包等，无需任何 UObject 开销：

```cpp
#include "Memory/StructPool.h"

struct FDamageContext {
    int32 AttackerId = 0;
    int32 VictimId = 0;
    float BaseDamage = 0.0f;
};

// 静态或系统持有
TStructPool<FDamageContext, 128> DamageContextPool;

void CalculateDamage() {
    // RAII 零堆碎片租约
    auto ScopedContext = DamageContextPool.AcquireScoped();
    ScopedContext->BaseDamage = 100.0f;
    // 离开大括号自动纳米级析构并回池
}
```

---

## 6. 关键注意事项与排错清单

- **句柄安全保护**：尽量使用 `TPooledObjectHandle<T>` 代替直接持有裸指针，若对象已在别处被归还或被他人复用，句柄会自动阻断访问，彻底消除隐蔽的数据窜号污染。
- **工作线程延迟归还**：若在物理子步或 TaskGraph 异步线程完成计算，使用 `Pool.ReleaseActorDeferred(Actor)` 压入无锁环形队列，主线程在帧首统一批量收割归还，避免主线程锁竞争。
- **深度组件清理**：系统已原生支持骨骼网格体动画重置（`EPoolResetFlags::Animation`）与 Pawn 控制器解绑（`EPoolResetFlags::PawnAndAI`），消除复用时的 T-Pose 闪烁与幽灵控制。

---

## 7. 弹性缩容与冷数据分帧淘汰 (TTL Eviction)

针对爆发性战斗场景（如短时间内发射上百发子弹）脱战后长时间闲置的内存占用，可配置生存期（TTL）与保底留存数量进行平滑缩容：

```cpp
FPoolEvictionSettings Settings;
Settings.bEnableEviction = true;              // 启用闲置超时自动淘汰
Settings.IdleTimeToLiveSeconds = 60.0f;       // 闲置超过 60 秒判定为冷实例
Settings.MinPreservedCount = 5;               // 保底保留 5 个，防止再次战斗发生冷启动
Settings.CheckIntervalSeconds = 5.0f;         // 每 5 秒心跳检测一次
Settings.MaxEvictionsPerStep = 2;             // 单步仅销毁 2 个，彻底杜绝单帧 GC 尖刺

// 应用配置到指定 Actor 或 UObject 池
Pool.SetActorPoolEvictionSettings(AMyProjectile::StaticClass(), Settings);
```

- **LIFO 与 FIFO 协同**：借出时遵循栈顶弹出最热实例（最大化 CPU 缓存局部性）；淘汰时遵循先进先出从头部扫描存活最久的冷实例，零额外性能损耗。
- **平滑销毁保障**：单步销毁严格受 `MaxEvictionsPerStep` 限制，将数百个实例的释放开销平摊在多帧内，消除任何可能的帧率抖动与 GC 尖刺。

---

## 8. 运行时性能监控与可观测性 (Observability)

### 8.1 控制台实时性能面板
在调试或性能调优过程中，在游戏控制台输入：
```bash
stat GamePool
```
可在屏幕上直观查看：
- **实时耗时 (Cycle Stats)**：`Acquire Actor Total`、`Release Actor Total`、`Component Deep Reset`、`Eviction Step Time` 等核心路径开销；
- **实例容量计数 (Counter Stats)**：`Active Actors` / `Idle Actors`、`Active UObjects` / `Idle UObjects`；
- **命中与穿透 (Counters)**：`Pool Cache Hits`（命中复用）与 `Pool Cache Misses`（穿透生成）。

### 8.2 Unreal Insights CPU 追踪
在 Insights 会话中，过滤 `GamePool` 即可捕获高精度 CPU 追踪事件：
- `GamePool_AcquireActor` / `GamePool_AcquireObject`：借出路径总耗时；
- `GamePool_ReleaseActor` / `GamePool_ReleaseObject`：归还入池耗时；
- `GamePool_ResetActorComponents`：组件深度状态清理与重置耗时；
- `GamePool_PrewarmStep`：分帧预热单步耗时；
- `GamePool_EvictObjects`：平滑淘汰单步耗时；
- `GamePool_ProcessDeferredReleases`：延迟收割多线程入池耗时。

### 8.3 运行时遥测快照与命中率
在蓝图或 C++ 中可随时获取高层遥测快照（`FPoolTelemetrySnapshot`）：
```cpp
UObjectPoolSubsystem* PoolSubsystem = UObjectPoolSubsystem::GetPtr(this);
if (PoolSubsystem != nullptr) {
    const FPoolTelemetrySnapshot Telemetry = PoolSubsystem->GetTelemetrySnapshot();
    const float HitRate = Telemetry.GetHitRatePercentage();
    
    // 上报 APM 监控或打印诊断
    UE_LOG(LogTemp, Log, TEXT("GamePool 命中率: %.2f%% (Hits: %lld, Misses: %lld, Active: %d, Idle: %d)"),
        HitRate, Telemetry.CacheHits, Telemetry.CacheMisses, Telemetry.TotalActiveCount, Telemetry.TotalIdleCount);
}
```

---

## 9. 纯 UObject 协程分帧预热与低内存应急泄洪 (Resilience & Async UObject)

### 9.1 纯 UObject C++20 协程分帧渐进预热
对非 Actor 纯 UObject（如战斗数值计算载荷、AI 决策树节点、UI 数据模型），同样支持流式/分帧渐进预热与取消打断，彻底杜绝帧率尖刺：
```cpp
// 基于类型或软对象路径分帧预热 50 个纯 UObject 实例，每帧生成 4 个
TGameCoroTask<int32> PrewarmTask = PoolSubsystem->PrewarmObjectPoolAsync(
    UMyDamagePayload::StaticClass(),
    50,
    4,
    CancellationToken
);
```

### 9.2 系统内存修剪广播与应急泄洪
- **引擎委托绑定**：子系统初始化时自动注册 `FCoreDelegates::GetMemoryTrimDelegate()`。当移动端或主机收到系统级低内存警告（Low Memory Trim）时，自动触发应急泄洪，安全销毁无保底的全部闲置实例；
- **保底留存机制**：若业务配置了 `MinPreservedCount`，内存修剪时将优先保留保底实例，防止次帧彻底冷启动；
- **主动全量泄洪**：关卡卸载、场景切换或大地图流送时，可主动调用清空所有空闲对象：
```cpp
// 主动释放所有闲置实例（bPreserveMinCount = false 彻底清零）
const int32 FlushedCount = PoolSubsystem->FlushAllIdleInstances(false);
```
