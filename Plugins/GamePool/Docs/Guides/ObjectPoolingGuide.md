# GamePool 对象与内存池实战指南

本指南介绍如何将自定义 `AActor`、非 Actor 纯 `UObject` 及原生 C++ 结构体接入 `UObjectPoolSubsystem` 与 `TStructPool`，并涵盖类型安全载荷注入、协程流式预热、TTL 弹性冷淘汰、多线程无锁延迟归还与低内存应急泄洪的工程最佳实践。

---

## 1. Actor 实现 `IGamePoolable` 与类型安全载荷解包

在头文件中包含 `Core/IGamePoolable.h` 并继承 `IGamePoolable`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Core/IGamePoolable.h"
#include "GameFramework/Actor.h"
#include "MyProjectile.generated.h"

struct FProjectileLaunchPayload {
    float Speed = 1500.0f;
    float Damage = 40.0f;
};

UCLASS()
class AMyProjectile : public AActor, public IGamePoolable {
    GENERATED_BODY()

public:
    AMyProjectile();

    virtual void OnAcquireFromPool_Implementation() override;
    virtual void OnAcquireFromPoolWithPayload(FPoolAcquirePayload InPayload) override;
    virtual void OnReleaseToPool_Implementation() override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProjectileMovementComponent> MovementComp;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USphereComponent> CollisionComp;

    float CurrentDamage = 0.0f;
};
```

在 `.cpp` 中实现借出唤醒、类型安全载荷提取与休眠清理：

```cpp
#include "MyProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

AMyProjectile::AMyProjectile() {
    PrimaryActorTick.bCanEverTick = true;
    CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
    RootComponent = CollisionComp;

    MovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MovementComp"));
}

void AMyProjectile::OnAcquireFromPool_Implementation() {
    MovementComp->SetUpdatedComponent(RootComponent);
    MovementComp->Velocity = GetActorForwardVector() * MovementComp->InitialSpeed;
    MovementComp->Activate(true);

    CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AMyProjectile::OnAcquireFromPoolWithPayload(FPoolAcquirePayload InPayload) {
    if (const FProjectileLaunchPayload* Launch = IGamePoolable::ExtractPayload<FProjectileLaunchPayload>(InPayload)) {
        CurrentDamage = Launch->Damage;
        MovementComp->Velocity = GetActorForwardVector() * Launch->Speed;
    }
}

void AMyProjectile::OnReleaseToPool_Implementation() {
    CurrentDamage = 0.0f;
    MovementComp->StopMovementImmediately();
    MovementComp->Deactivate();

    CollisionComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
```

> **注意**：借出携带有效载荷时，引擎先调用 `OnAcquireFromPool`，再调用 `OnAcquireFromPoolWithPayload`，请勿在 `OnAcquireFromPoolWithPayload` 中手动重复调用 `OnAcquireFromPool_Implementation()`。

---

## 2. 三种借还模式对比

### 模式 A：RAII 作用域租约（推荐用于函数作用域内的短生命周期对象）
```cpp
#include "Subsystem/UObjectPoolSubsystem.h"
#include "MyProjectile.h"

void AMyWeapon::SpawnTransientHitBox() {
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);

    const FProjectileLaunchPayload Payload{2000.0f, 60.0f};
    TGameStatusResult<TScopedPoolRental<AMyProjectile>> RentalRes =
        Pool.AcquireScopedActorWithPayload<AMyProjectile>(AMyProjectile::StaticClass(), GetActorTransform(), Payload);
    if (RentalRes.IsSuccess()) {
        TScopedPoolRental<AMyProjectile> Rental = RentalRes.ExtractValue();
        Rental->PerformDetection();
    } // 离开作用域自动归还回池，杜绝异常路径泄漏
}
```

### 模式 B：世代安全句柄（推荐用于跨帧、定时器与异步回调持有）
```cpp
void AMyWeapon::FireAsync() {
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);

    TGameStatusResult<TPooledObjectHandle<AMyProjectile>> HandleRes =
        Pool.AcquireActorHandle<AMyProjectile>(AMyProjectile::StaticClass(), GetMuzzleTransform());
    if (HandleRes.IsSuccess()) {
        TPooledObjectHandle<AMyProjectile> BulletHandle = HandleRes.GetValue();

        GetWorldTimerManager().SetTimer(TimerHandle, [BulletHandle, WeakPool = TWeakObjectPtr<UObjectPoolSubsystem>(&Pool)]() {
            if (UObjectPoolSubsystem* ValidPool = WeakPool.Get()) {
                if (BulletHandle.IsValid()) {
                    (void)ValidPool->ReleaseActor(BulletHandle);
                }
            }
        }, 3.0f, false);
    }
}
```
若子弹提前命中并归还，旧句柄在回调触发时自动失效（`IsValid() == false`），即使该对象已被复用给下一发子弹也绝不会发生串号误回收。

### 模式 C：强类型指针直借（高性能热循环与蓝图门面）
```cpp
void AMyWeapon::FireDirect() {
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);

    TGameStatusResult<AMyProjectile*> BulletRes =
        Pool.AcquireActor<AMyProjectile>(AMyProjectile::StaticClass(), GetMuzzleTransform());
    if (BulletRes.IsSuccess()) {
        AMyProjectile* Bullet = BulletRes.GetValue();
        // 使用完毕后归还
        (void)Pool.ReleaseActor(Bullet);
    }
}
```

---

## 3. 事件驱动 C++20 协程流式预热

在大世界关卡加载、武器切换或刷怪波次来临前，可使用 `TSoftClassPtr` 或 `FSoftObjectPath` 发起事件驱动流式加载与分帧预热（零逐帧轮询，触达 `MaxIdleCount` 自动短路，支持 `FCancellationToken` 中途熔断）：

```cpp
TGameCoroTask<void> AMyWeapon::PrewarmAmmoAsync(
    TSoftClassPtr<AActor> AmmoSoftClass,
    FCancellationToken CancellationToken
) {
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);

    const int32 PrewarmedActors = co_await Pool.PrewarmAsync(AmmoSoftClass, 20, 2, CancellationToken);
    GAME_LOG(Log, TEXT("Actor 预热完成，生成 %d 个实例"), PrewarmedActors);
}

TGameCoroTask<void> AMyCombatManager::PrewarmContextsAsync(
    TSoftClassPtr<UObject> ContextSoftClass,
    FCancellationToken CancellationToken
) {
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(this);

    const int32 PrewarmedObjects = co_await Pool.PrewarmObjectPoolAsync(ContextSoftClass, 50, 4, CancellationToken);
    GAME_LOG(Log, TEXT("纯 UObject 预热完成，生成 %d 个实例"), PrewarmedObjects);
}
```

---

## 4. 跨线程无锁延迟归还

在物理子步、TaskGraph 工作线程或异步回调中，禁止直接调用 `ReleaseActor` / `ReleaseObject`。请使用无锁 MPMC 延迟归还通道：

```cpp
// 工作线程：无锁压入环形缓冲（1024 槽位；ReleaseObjectDeferred 自动拒绝 AActor 实例）
const TGameStatusResult<void> EnqueueRes = Pool.ReleaseActorDeferred(ActorToReturn);
if (EnqueueRes.IsFailure() && EnqueueRes.GetError().ErrorCode == Game::PoolErrorCodes::DeferredQueueFull) {
    // 队列满时可记录告警或交由主线程优先收割
}

// 主线程帧首：按配额批量收割并安全执行组件重置与回调
const int32 Pending = Pool.GetPendingDeferredReleaseCount();
if (Pending > 0) {
    const int32 Harvested = Pool.ProcessDeferredReleases(64);
}
```

---

## 5. 原生 C++ 结构体内存池 (`TStructPool`)

针对非 UObject 的高频战斗结算上下文、几何查询结果包与网络解包结构体，使用 `Memory/TStructPool.h` 彻底消除 `new`/`delete` 与堆碎片：

```cpp
#include "Memory/TStructPool.h"

struct FDamageContext {
    int32 AttackerId = 0;
    int32 VictimId = 0;
    float BaseDamage = 0.0f;
};

TStructPool<FDamageContext, 128> DamageContextPool;

void CalculateDamage(int32 AttackerId, int32 VictimId) {
    TScopedStructRental<FDamageContext, 128> ScopedContext = DamageContextPool.AcquireScoped();
    ScopedContext->AttackerId = AttackerId;
    ScopedContext->VictimId = VictimId;
    ScopedContext->BaseDamage = 100.0f;
} // 离开大括号原地析构并纳秒级回池
```

---

## 6. 弹性缩容、低内存应急泄洪与性能调优

### 6.1 TTL 弹性冷淘汰
```cpp
FPoolEvictionSettings Settings;
Settings.bEnableEviction = true;
Settings.IdleTimeToLiveSeconds = 60.0f;
Settings.MinPreservedCount = 5;
Settings.CheckIntervalSeconds = 5.0f;
Settings.MaxEvictionsPerStep = 2;

Pool.SetActorPoolEvictionSettings(AMyProjectile::StaticClass(), Settings);
```
- **LIFO + FIFO 协同**：底层 `TDeque<FIdleEntry>` 从队尾弹出最热实例（提升 L1/L2 缓存命中率），从队首 $O(1)$ 逐出最冷超时实例。
- **动态调度零空转**：调度器托管于 `GameCore::UThrottledTickerSubsystem`，自动按全部启用池的最小 `CheckIntervalSeconds` 心跳；全部池关闭淘汰后自动注销定时句柄。
- **按位裁剪组件重置**：对纯逻辑或轻量 Actor，可通过 `Pool.SetPoolResetFlags(ActorClass, EPoolResetFlags::Timers | EPoolResetFlags::Movement)` 精准跳过不必要的组件分支，将归还开销压降至约 `1.2 µs`。

### 6.2 低内存应急泄洪与 GC 安全
- `UObjectPoolSubsystem` 自动监听 `FCoreDelegates::GetMemoryTrimDelegate()`，在系统低内存警告或 `UEngine::TrimMemory()` 时自动按各池 `MinPreservedCount` 优先从冷端泄洪。
- **常规 GC 零误清**：控制器自动过滤常规 GC / 增量清理期间引擎内部发出的 `FMemory::Trim()` 广播（`IsGarbageCollecting() || IsIncrementalPurgePending()`），确保周期性 GC 既不非法重入销毁对象，也不会误清预热好的池内缓存。
- 场景切换时可主动全量清池：
  ```cpp
  const int32 FlushedCount = Pool.FlushAllIdleInstances(false);
  ```

### 6.3 监控与诊断
- 控制台输入 `stat GamePool` 查看实时 Cycle/Counter 统计，或通过 `LogGamePool` 分类（由 `GameLogging` 统一管理）筛选对象池诊断日志。
- Unreal Insights 过滤 `GamePool_` 查看 `GamePool_AcquireActor`、`GamePool_ReleaseActor`、`GamePool_AcquireObject`、`GamePool_ReleaseObject`、`GamePool_ResetActorComponents`、`GamePool_PrewarmStep`、`GamePool_PrewarmObjectStep`、`GamePool_EvictActors`、`GamePool_EvictObjects`、`GamePool_ProcessDeferredReleases`。
- 运行期随时调用 `Pool.GetTelemetrySnapshot()` 获取 `CacheHits`、`CacheMisses`、`TotalAcquires`、`TotalActiveCount`、`TotalIdleCount` 与 `GetHitRatePercentage()`。
