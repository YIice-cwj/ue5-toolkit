# GameCore 对象池实战指南

本指南详细介绍如何让一个自定义 Actor 接入 `UObjectPoolSubsystem` 对象池，实现高性能复用与平滑休眠唤醒。

---

## 1. Actor 实现 `IGamePoolable` 接口

在您的 Actor 头文件中继承 `IGamePoolable`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pool/IGamePoolable.h"
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
    // 1. 重置弹道运动组件状态
    MovementComp->SetUpdatedComponent(RootComponent);
    MovementComp->Velocity = GetActorForwardVector() * MovementComp->InitialSpeed;
    MovementComp->Activate(true);

    // 2. 开启碰撞并重置击中计数
    CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AMyProjectile::OnReleaseToPool_Implementation()
{
    // 1. 停止弹道运动与飞行音效
    MovementComp->StopMovementImmediately();
    MovementComp->Deactivate();

    // 2. 禁用碰撞，防止休眠在隐蔽点时误触周围触发器
    CollisionComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
```

---

## 3. 在发射器（Weapon / Spawner）中调用

```cpp
#include "Pool/ObjectPoolSubsystem.h"
#include "MyProjectile.h"

void AMyWeapon::Fire()
{
    UWorld* World = GetWorld();
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(World);

    // 1. （可选）在游戏关卡加载时预热，生成 50 发子弹常驻内存
    // Pool.Prewarm(AMyProjectile::StaticClass(), 50);

    // 2. 开火时从池中借出（模板方法自动 Cast）
    FTransform SpawnTransform(GetMuzzleRotation(), GetMuzzleLocation());
    AMyProjectile* Bullet = Pool.AcquireActor<AMyProjectile>(AMyProjectile::StaticClass(), SpawnTransform);
}
```

---

## 4. 关键注意事项与排错清单

- **严禁在归还后继续持有裸指针**：调用 `ReleaseActor(Actor)` 后，调用方必须立即将持有的指针置空（`Bullet = nullptr;`）。
- **对象池内置深层状态清理**：`ReleaseActor` 会自动帮您重置物理线速度/角速度、清理挂接的所有定时器、停止播放中的音效与粒子发射；您只需在 `OnReleaseToPool` 中处理业务自定义状态。
- **外部意外销毁自愈保障**：若借出的 Actor 在池外被直接 `Destroy()`，对象池会自动监听 `OnDestroyed` 并自愈扣减 `ActiveCount`，杜绝计数永久虚高。

