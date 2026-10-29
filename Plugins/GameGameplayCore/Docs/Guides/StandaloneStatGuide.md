# GameGameplayCore 自持模式实战指南 (Standalone)

在大量轻量游戏实体（如可破坏木桶、防御工事、非 GAS 普通小怪、训练假人）中，不需要复杂的属性集（AttributeSet）和技能系统。`UGameStatComponent` 的 **Standalone 自持模式** 提供了一套开箱即用、开发者通过 Tag 自由注册所需属性且具备完整网络同步的极简解决方案。

---

## 1. 核心工作原理

在 Standalone 模式下：
- `UGameStatComponent` 内部维护属性表（`ReplicatedStats` 复制数组 + `FGameplayTag` 索引）；
- 所有已注册属性通过 `ReplicatedStats` 自动同步至客户端；
- 若配置了 `PrimaryVitalTag`，主生命属性（如血量）耗尽时自动迁移生死状态；
- 内存开销极低，零 GAS 运行时消耗。

---

## 2. 场景可破坏物件实战（木桶案例）

### 蓝图/C++ 均适用的实现方式

```cpp
#include "GameFramework/Actor.h"
#include "Damage/IDamageable.h"
#include "Stat/UGameStatComponent.h"
#include "Components/StaticMeshComponent.h"

UCLASS()
class ADestructibleBarrel : public AActor, public IDamageable {
    GENERATED_BODY()

public:
    ADestructibleBarrel() {
        PrimaryActorTick.bCanEverTick = false;

        MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
        RootComponent = MeshComp;

        StatComp = CreateDefaultSubobject<UGameStatComponent>(TEXT("StatComp"));

        // 注册木桶血量，并设为主生命属性
        const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health"));
        StatComp->RegisterStat(HealthTag, 50.0f, 50.0f, 0.0f);
        StatComp->SetPrimaryVitalTag(HealthTag);
    }

    virtual void BeginPlay() override {
        Super::BeginPlay();
        // 绑定主生命状态变更，破碎表现
        StatComp->OnVitalStateChangedNative.AddWeakLambda(this, [this](bool bIsAlive, const FGameplayTag&) {
            if (!bIsAlive) {
                ExplodeAndDestroy();
            }
        });
    }

    virtual FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) override {
        const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health"));
        float ActualDelta = 0.0f;
        StatComp->ModifyStat(HealthTag, -Context.GetTotalBaseDamage(), ActualDelta);

        FGameDamageResult Result;
        Result.SetActualDamage(FMath::Abs(ActualDelta));
        Result.AddFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Hit")));
        if (!StatComp->IsVitalAlive()) {
            Result.AddFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Fatal")));
        }
        return Result;
    }

    void ExplodeAndDestroy() {
        // 生成破碎碎片粒子、播放音效、销毁自身
        Destroy();
    }

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> MeshComp;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UGameStatComponent> StatComp;
};
```

---

## 3. 修正器叠加与按 Tag 订阅

Standalone 模式同样支持装备 / 天赋 / Buff 的临时属性修正，无需改动基础值：

```cpp
const FGameplayTag CritTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.CritChance"));

const uint32 CritHandle = StatComp->AddStatModifier(
    CritTag,
    FGameStatModifier(0.15f, EGameStatModifierOp::Add));

const float EffectiveCrit = StatComp->GetStatEffectiveCurrent(CritTag);

StatComp->RemoveStatModifier(CritTag, CritHandle);
```

- 基础值读 `GetStatCurrent`，叠加修正后的值读 `GetStatEffectiveCurrent`。
- UI 可按 Tag 精确订阅，并在订阅时立即拿到当前值：

```cpp
HealthSubscription = StatComp->SubscribeStatChanged(
    HealthTag,
    FOnGameStatChangedNative::FDelegate::CreateWeakLambda(this,
        [this](const FGameplayTag&, float OldValue, float NewValue, float MaxValue) {
            UpdateHealthBar(NewValue, MaxValue);
        }));
```

---

## 4. 复活与重置契约

对于可重用的怪物或需要重生的实体，直接调用 `ReviveVital()` 即可重置状态：

```cpp
void ARespawnableActor::Respawn()
{
    // 将主生命属性从 0 重置为满值，并恢复存活状态
    StatComp->ReviveVital(1.0f);
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
}
```
此时 `OnStatChanged`（数值重置为满值）与 `OnVitalStateChanged`（状态重置为 Alive）委托会被自动触发，UI 与状态指示器自动刷新。

