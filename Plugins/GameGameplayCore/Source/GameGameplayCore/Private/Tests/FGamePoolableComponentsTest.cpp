/**
 * @file FGamePoolableComponentsTest.cpp
 * @brief 核心战斗组件对象池生命周期重置契约 (IGamePoolable) 自动化测试
 */

#include "CoreMinimal.h"
#include "Buff/UGameBuffComponent.h"
#include "Core/IGamePoolable.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Stat/UGameStatComponent.h"
#include "Stat/UGameVitalComponent.h"
#include "Tests/GameGameplayCoreTestUtils.h"

#if WITH_AUTOMATION_TESTS

using Game::GameplayCore::Test::FScopedTestHostActor;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolableComponentsLifecycleTest,
    "Game.GameplayCore.Poolable.ComponentLifecycleReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Stat/Buff/Vital 三大战斗组件在对象池借还时的静默重置、不误播复活、代际句柄防 ABA 与单调实例 ID
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGamePoolableComponentsLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    TestTrue(TEXT("宿主 Actor 创建成功"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Stat.Poolable.Health")));
    const FGameplayTag AttackBuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Poolable.AttackBoost")));

    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(Host.Get());
    StatComp->RegisterComponent();

    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(Host.Get());
    VitalComp->BindStatComponent(StatComp);
    VitalComp->SetPrimaryVitalTag(HealthTag);
    VitalComp->RegisterComponent();

    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(Host.Get());
    BuffComp->RegisterComponent();
    BuffComp->BindStatComponent(StatComp);

    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("初始生命值为 100"), StatComp->GetStatCurrent(HealthTag), 100.0f);
    TestTrue(TEXT("初始存活状态为 true"), VitalComp->IsVitalAlive());

    const FGameStatModifierHandle PreResetHandle =
        StatComp->AddStatModifier(HealthTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add)).GetValue();
    TestEqual(TEXT("修正后有效生命上限为 120"), StatComp->GetStatEffectiveMax(HealthTag), 120.0f);

    StatComp->SetStatCurrent(HealthTag, 0.0f);
    StatComp->RemoveStatModifier(HealthTag, PreResetHandle);
    TestFalse(TEXT("生命归零后判定阵亡"), VitalComp->IsVitalAlive());

    const FGameStatModifierHandle StaleHandle =
        StatComp->AddStatModifier(HealthTag, FGameStatModifier(15.0f, EGameStatModifierOp::Add, false, true)).GetValue();

    FGameBuffSpec BuffSpec;
    BuffSpec.BuffTag = AttackBuffTag;
    BuffSpec.Duration = 10.0f;
    BuffSpec.MaxStacks = 3;
    FGameStatModifierSpec HealthGuardMod;
    HealthGuardMod.StatTag = HealthTag;
    HealthGuardMod.Modifier = FGameStatModifier(50.0f, EGameStatModifierOp::Add, true, true);
    BuffSpec.Modifiers.Add(HealthGuardMod);
    const int32 FirstBuffId = BuffComp->K2_ApplyBuff(BuffSpec);
    TestTrue(TEXT("Buff 施加成功且 ID 有效"), FirstBuffId > 0);
    TestTrue(TEXT("限时 Buff 开启了 Tick"), BuffComp->IsComponentTickEnabled());
    TestTrue(TEXT("挂接生命 Buff 后有效生命抬升为 50 且恢复存活"), VitalComp->IsVitalAlive());

    int32 StatReleaseBroadcasts = 0;
    int32 VitalReleaseBroadcasts = 0;
    StatComp->OnStatChangedNative.AddLambda([&StatReleaseBroadcasts](const FGameplayTag&, float, float, float) {
        ++StatReleaseBroadcasts;
    });
    VitalComp->OnVitalStateChangedNative.AddLambda([&VitalReleaseBroadcasts](bool, const FGameplayTag&) {
        ++VitalReleaseBroadcasts;
    });

    IGamePoolable::Execute_OnReleaseToPool(BuffComp);
    TestFalse(TEXT("归还对象池后 Buff 已清空"), BuffComp->HasBuff(AttackBuffTag));
    TestEqual(TEXT("归还对象池后 Buff 层数为 0"), BuffComp->GetBuffStacks(AttackBuffTag), 0);
    TestFalse(TEXT("归还对象池后 Tick 已关闭"), BuffComp->IsComponentTickEnabled());
    TestEqual(TEXT("Buff 先于 Stat 入池时静默回滚生命修正器至 0"), StatComp->GetStatEffectiveCurrent(HealthTag), 0.0f);
    TestEqual(TEXT("Buff 先于 Stat 入池不触发 Stat 业务广播"), StatReleaseBroadcasts, 0);
    TestEqual(TEXT("Buff 先于 Stat 入池不误触发 Vital 死亡事件"), VitalReleaseBroadcasts, 0);

    IGamePoolable::Execute_OnReleaseToPool(StatComp);
    TestEqual(TEXT("归还对象池后生命值静默重置回最大值 100"), StatComp->GetStatCurrent(HealthTag), 100.0f);
    TestEqual(TEXT("归还对象池后修饰器已清空，有效上限回落为 100"), StatComp->GetStatEffectiveMax(HealthTag), 100.0f);
    TestEqual(TEXT("Stat 入池静默重置不触发业务属性广播"), StatReleaseBroadcasts, 0);
    TestEqual(TEXT("死亡实体 Stat 先入池不误触发 Vital 复活事件"), VitalReleaseBroadcasts, 0);

    IGamePoolable::Execute_OnReleaseToPool(VitalComp);
    TestTrue(TEXT("归还对象池后存活状态静默重置为 true"), VitalComp->IsVitalAlive());
    TestEqual(TEXT("Vital 入池静默重置不触发业务复活广播"), VitalReleaseBroadcasts, 0);

    IGamePoolable::Execute_OnAcquireFromPool(BuffComp);
    IGamePoolable::Execute_OnAcquireFromPool(StatComp);
    IGamePoolable::Execute_OnAcquireFromPool(VitalComp);
    TestTrue(TEXT("再次出库后组件状态正常且保持存活"), VitalComp->IsVitalAlive());

    const FGameStatModifierHandle NewHandle =
        StatComp->AddStatModifier(HealthTag, FGameStatModifier(30.0f, EGameStatModifierOp::Add, false, true)).GetValue();
    TestTrue(TEXT("池化前签发的旧句柄无法误删新生命周期修正器"), StatComp->RemoveStatModifier(HealthTag, StaleHandle).IsFailure());
    TestEqual(TEXT("新生命周期修正器保持生效"), StatComp->GetStatEffectiveMax(HealthTag), 130.0f);
    TestTrue(TEXT("新句柄精确移除成功"), StatComp->RemoveStatModifier(HealthTag, NewHandle).IsSuccess());

    const int32 SecondBuffId = BuffComp->K2_ApplyBuff(BuffSpec);
    TestTrue(TEXT("跨池化周期 Buff 实例 ID 保持严格单调递增防联机碰撞"), SecondBuffId > FirstBuffId);

    return true;
}

#endif
