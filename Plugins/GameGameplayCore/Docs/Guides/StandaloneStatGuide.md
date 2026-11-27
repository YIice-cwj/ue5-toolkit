# GameGameplayCore 自持模式实战指南 (Standalone)

在大量轻量游戏实体（如可破坏木桶、防御工事、非 GAS 普通小怪、训练假人）中，不需要复杂的属性集（AttributeSet）和技能系统。`UGameStatComponent` 的 **Standalone 自持模式** 提供了一套开箱即用、开发者通过 Tag 自由注册所需属性且具备完整网络同步的极简解决方案。

---

## 1. 核心工作原理

在 Standalone 模式下：
- `UGameStatComponent` 内部维护属性表（`ReplicatedStats` 复制数组 + `FGameplayTag` 索引）；
- 所有已注册属性通过 `ReplicatedStats` 自动同步至客户端；
- 挂载并关联 `UGameVitalComponent`（配置 `PrimaryVitalTag`）后，主生命属性（如血量）耗尽时自动迁移生死状态；
- 内存开销极低，零 GAS 运行时消耗。

---

## 2. 场景可破坏物件实战（木桶案例）

```cpp
#include "GameFramework/Actor.h"
#include "Damage/IDamageable.h"
#include "Stat/UGameStatComponent.h"
#include "Stat/UGameVitalComponent.h"
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
        VitalComp = CreateDefaultSubobject<UGameVitalComponent>(TEXT("VitalComp"));
    }

    void BeginPlay() override {
        Super::BeginPlay();

        // 在 BeginPlay 注册木桶血量并绑定主生命状态机（避免 CDO 构造期查询 GameplayTag）
        const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health"));
        StatComp->RegisterStat(HealthTag, 50.0f, 50.0f, 0.0f);
        VitalComp->BindStatComponent(StatComp);
        VitalComp->SetPrimaryVitalTag(HealthTag);
        VitalComp->OnVitalStateChangedNative.AddWeakLambda(this, [this](bool bIsAlive, const FGameplayTag&) {
            if (!bIsAlive) {
                ExplodeAndDestroy();
            }
        });
    }

    FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) override {
        const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Health"));
        float ActualDelta = 0.0f;
        StatComp->ModifyStat(HealthTag, -Context.GetTotalBaseDamage(), ActualDelta);

        FGameDamageResult Result;
        Result.SetActualDamage(FMath::Abs(ActualDelta));
        Result.AddFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Hit")));
        if (!VitalComp->IsVitalAlive()) {
            Result.AddFeedbackTag(FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Fatal")));
        }
        return Result;
    }

    void ExplodeAndDestroy() {
        Destroy();
    }

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> MeshComp;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UGameStatComponent> StatComp;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UGameVitalComponent> VitalComp;
};
```

---

## 3. 修正器叠加、原子更新与按 Tag 订阅

Standalone 模式支持装备 / 天赋 / Buff 的临时属性修正，无需改动基础值，并通过强类型代际句柄 `FGameStatModifierHandle` 精确管理：

```cpp
const FGameplayTag CritTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.CritChance"));

const TGameStatusResult<FGameStatModifierHandle> CritRes = StatComp->AddStatModifier(
    CritTag,
    FGameStatModifier(0.15f, EGameStatModifierOp::Add));

if (CritRes.IsSuccess()) {
    const FGameStatModifierHandle CritHandle = CritRes.GetValue();
    const float EffectiveCrit = StatComp->GetStatEffectiveCurrent(CritTag);

    // 原地原子更新，零中间态回落
    StatComp->UpdateStatModifier(CritTag, CritHandle, FGameStatModifier(0.25f, EGameStatModifierOp::Add));

    // 按强类型代际句柄精确注销
    StatComp->RemoveStatModifier(CritTag, CritHandle);
}
```

- 涉及多条修正器同时替换时，可使用 `UGameStatComponent::FScopedModifierBatch Batch(*StatComp);` 将整组变动合并为单次终态差分广播。
- UI 可按 Tag 精确订阅，并在订阅时立即拿到当前有效值：

```cpp
HealthSubscription = StatComp->SubscribeStatChanged(
    this,
    HealthTag,
    [this](const FGameplayTag&, float OldValue, float NewValue, float MaxValue) {
        UpdateHealthBar(NewValue, MaxValue);
    });
```

---

## 4. 复活与对象池重置契约

- **游戏内复活**：调用 `VitalComp->ReviveVital(1.0f)` 按 `[Min, Max]` 比例恢复生命并触发 `OnStatChanged` 与 `OnVitalStateChanged` 广播。
- **对象池复用**：池化 Actor 归还对象池时分别触发 `IGamePoolable::Execute_OnReleaseToPool(BuffComp / StatComp / VitalComp)`，三大组件均执行静默内部状态重置（不误播游戏态复活事件），再次出库时触发 `OnAcquireFromPool` 即可恢复就绪态。
