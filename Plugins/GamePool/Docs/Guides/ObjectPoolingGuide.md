# GamePool 对象与内存池实战指南

本指南详细介绍如何让自定义 Actor、纯 UObject 或原生 C++ 结构体接入 `UObjectPoolSubsystem` 与 `TStructPool`，实现零野指针、高吞吐复用与平滑休眠唤醒。

---

## 1. Actor 实现 `IGamePoolable` 接口

在您的 Actor 头文件中继承 `IGamePoolable`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObjectPool/IGamePoolable.h"
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
#include "ObjectPool/ObjectPoolSubsystem.h"
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
#include "MemoryPool/StructPool.h"

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
