/**
 * @file FGamePoolableComponentsTest.cpp
 * @brief 核心战斗组件对象池生命周期重置契约 (IGamePoolable) 自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Core/IGamePoolable.h"
#include "Buff/UGameBuffComponent.h"
#include "Stat/UGameStatComponent.h"
#include "Stat/UGameVitalComponent.h"

#if WITH_AUTOMATION_TESTS

namespace {

UWorld* GetPoolableTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

AActor* SpawnPoolableHost(UWorld* InWorld) {
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return InWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolableComponentsLifecycleTest,
    "Game.GameplayCore.Poolable.ComponentLifecycleReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Stat/Buff/Vital 三大战斗组件在对象池借还 (IGamePoolable) 时的自愈与状态重置
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGamePoolableComponentsLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetPoolableTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }

    AActor* HostActor = SpawnPoolableHost(World);
    TestNotNull(TEXT("宿主 Actor 创建成功"), HostActor);
    if (HostActor == nullptr) {
        return false;
    }

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Stat.Poolable.Health")));
    const FGameplayTag AttackBuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Poolable.AttackBoost")));

    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(HostActor);
    StatComp->RegisterComponent();

    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(HostActor);
    VitalComp->SetPrimaryVitalTag(HealthTag);
    VitalComp->RegisterComponent();

    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(HostActor);
    BuffComp->RegisterComponent();
    BuffComp->BindStatComponent(StatComp);

    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("初始生命值为 100"), StatComp->GetStatCurrent(HealthTag), 100.0f);
    TestTrue(TEXT("初始存活状态为 true"), VitalComp->IsVitalAlive());

    float ActualDelta = 0.0f;
    StatComp->ModifyStat(HealthTag, -60.0f, ActualDelta);
    TestEqual(TEXT("生命值扣减后为 40"), StatComp->GetStatCurrent(HealthTag), 40.0f);

    StatComp->AddStatModifier(HealthTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add));
    TestEqual(TEXT("修正后有效生命上限为 120"), StatComp->GetStatEffectiveMax(HealthTag), 120.0f);

    FGameBuffSpec BuffSpec;
    BuffSpec.BuffTag = AttackBuffTag;
    BuffSpec.Duration = 10.0f;
    BuffSpec.MaxStacks = 3;
    const int32 BuffInstanceId = BuffComp->K2_ApplyBuff(BuffSpec);
    TestTrue(TEXT("Buff 施加成功且 ID 有效"), BuffInstanceId > 0);
    TestTrue(TEXT("持有 Buff"), BuffComp->HasBuff(AttackBuffTag));
    TestTrue(TEXT("限时 Buff 开启了 Tick"), BuffComp->IsComponentTickEnabled());

    IGamePoolable::Execute_OnReleaseToPool(BuffComp);
    TestFalse(TEXT("归还对象池后 Buff 已清空"), BuffComp->HasBuff(AttackBuffTag));
    TestEqual(TEXT("归还对象池后 Buff 层数为 0"), BuffComp->GetBuffStacks(AttackBuffTag), 0);
    TestFalse(TEXT("归还对象池后 Tick 已关闭"), BuffComp->IsComponentTickEnabled());

    IGamePoolable::Execute_OnReleaseToPool(StatComp);
    TestEqual(TEXT("归还对象池后生命值重置回最大值 100"), StatComp->GetStatCurrent(HealthTag), 100.0f);
    TestEqual(TEXT("归还对象池后修饰器已清空，有效上限回落为 100"), StatComp->GetStatEffectiveMax(HealthTag), 100.0f);

    StatComp->SetStatCurrent(HealthTag, 0.0f);
    VitalComp->EvaluateVitalState();
    TestFalse(TEXT("生命归零后判定阵亡"), VitalComp->IsVitalAlive());

    IGamePoolable::Execute_OnReleaseToPool(VitalComp);
    TestTrue(TEXT("归还对象池后存活状态重置为 true"), VitalComp->IsVitalAlive());
    TestEqual(TEXT("归还对象池后主生命数值同步重置回 100"), StatComp->GetStatCurrent(HealthTag), 100.0f);

    IGamePoolable::Execute_OnAcquireFromPool(BuffComp);
    IGamePoolable::Execute_OnAcquireFromPool(StatComp);
    IGamePoolable::Execute_OnAcquireFromPool(VitalComp);
    TestTrue(TEXT("再次出库后组件状态正常且保持存活"), VitalComp->IsVitalAlive());

    HostActor->Destroy();
    return true;
}

#endif
