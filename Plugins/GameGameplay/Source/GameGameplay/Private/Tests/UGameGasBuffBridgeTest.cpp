/**
 * @file UGameGasBuffBridgeTest.cpp
 * @brief GAS Buff 桥接层自动化测试套件
 */

#include "Bridge/UGameGasBuffBridge.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGasBuffBridgeTest,
    "Game.Gameplay.GasBuffBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameGasBuffBridgeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FActiveGameplayEffectHandle InvalidHandle;
    TestFalse(TEXT("空 ASC 施加 Buff 失败"), UGameGasBuffBridge::ApplyBuffEffect(nullptr, nullptr).IsValid());
    TestFalse(TEXT("空 ASC 查询激活失败"), UGameGasBuffBridge::IsBuffEffectActive(nullptr, InvalidHandle));
    TestFalse(TEXT("空 ASC 移除 Buff 失败"), UGameGasBuffBridge::RemoveBuffEffect(nullptr, InvalidHandle));
    TestEqual(TEXT("空 ASC 查询叠层返回 0"), UGameGasBuffBridge::GetBuffStackCount(nullptr, InvalidHandle), 0);
    TestEqual(TEXT("空 ASC 查询剩余时间返回 0.0"), UGameGasBuffBridge::GetBuffRemainingTime(nullptr, InvalidHandle), 0.0f);
    TestEqual(TEXT("空 ASC 查询总时长返回 0.0"), UGameGasBuffBridge::GetBuffDuration(nullptr, InvalidHandle), 0.0f);

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("测试 UWorld 创建成功"), World);
    if (World == nullptr) {
        return false;
    }

    AActor* DummyActor = World->SpawnActor<AActor>();
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(DummyActor);
    ASC->RegisterComponent();
    ASC->InitAbilityActorInfo(DummyActor, DummyActor);

    TestFalse(TEXT("有效 ASC 但空效果类施加失败"), UGameGasBuffBridge::ApplyBuffEffect(ASC, nullptr).IsValid());
    TestFalse(TEXT("无效句柄查询激活失败"), UGameGasBuffBridge::IsBuffEffectActive(ASC, InvalidHandle));

    const FActiveGameplayEffectHandle NonExistentHandle = FActiveGameplayEffectHandle::GenerateNewHandle(ASC);
    TestFalse(TEXT("有效 ASC 但不存在句柄查询激活返回 false"), UGameGasBuffBridge::IsBuffEffectActive(ASC, NonExistentHandle));
    TestEqual(TEXT("有效 ASC 但不存在句柄查询叠层返回 0"), UGameGasBuffBridge::GetBuffStackCount(ASC, NonExistentHandle), 0);
    TestEqual(TEXT("有效 ASC 但不存在句柄查询剩余时间返回 0.0"), UGameGasBuffBridge::GetBuffRemainingTime(ASC, NonExistentHandle), 0.0f);
    TestEqual(TEXT("有效 ASC 但不存在句柄查询总时长返回 0.0"), UGameGasBuffBridge::GetBuffDuration(ASC, NonExistentHandle), 0.0f);
    TestFalse(TEXT("有效 ASC 但不存在句柄移除返回 false"), UGameGasBuffBridge::RemoveBuffEffect(ASC, NonExistentHandle));

    UGameplayEffect* BaseCDO = GetMutableDefault<UGameplayEffect>();
    const EGameplayEffectDurationType OldDurationPolicy = BaseCDO->DurationPolicy;
    const FGameplayEffectModifierMagnitude OldDurationMagnitude = BaseCDO->DurationMagnitude;
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    const EGameplayEffectStackingType OldStackingType = BaseCDO->StackingType;
    const int32 OldStackLimitCount = BaseCDO->StackLimitCount;
    BaseCDO->DurationPolicy = EGameplayEffectDurationType::Infinite;
    BaseCDO->StackingType = EGameplayEffectStackingType::AggregateByTarget;
    BaseCDO->StackLimitCount = 5;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

    const FActiveGameplayEffectHandle Handle = UGameGasBuffBridge::ApplyBuffEffect(ASC, UGameplayEffect::StaticClass(), 1, DummyActor);
    TestTrue(TEXT("施加无限时长 Buff 成功且返回有效句柄"), Handle.IsValid());
    TestTrue(TEXT("Buff 处于激活状态"), UGameGasBuffBridge::IsBuffEffectActive(ASC, Handle));
    TestEqual(TEXT("首次施加叠层数为 1"), UGameGasBuffBridge::GetBuffStackCount(ASC, Handle), 1);
    TestEqual(TEXT("无限时长 Buff 持续时间返回 -1.0"), UGameGasBuffBridge::GetBuffDuration(ASC, Handle), -1.0f);

    const FActiveGameplayEffectHandle SecondHandle = UGameGasBuffBridge::ApplyBuffEffect(ASC, UGameplayEffect::StaticClass(), 1, DummyActor);
    TestTrue(TEXT("再次施加叠层成功"), SecondHandle.IsValid());
    TestEqual(TEXT("叠层数自增为 2"), UGameGasBuffBridge::GetBuffStackCount(ASC, Handle), 2);

    TestTrue(TEXT("成功移除 1 层 Buff"), UGameGasBuffBridge::RemoveBuffEffect(ASC, Handle, 1));
    TestEqual(TEXT("扣减后剩余 1 层"), UGameGasBuffBridge::GetBuffStackCount(ASC, Handle), 1);
    TestTrue(TEXT("扣减 1 层后 Buff 仍然激活"), UGameGasBuffBridge::IsBuffEffectActive(ASC, Handle));

    TestTrue(TEXT("全部移除 Buff 成功"), UGameGasBuffBridge::RemoveBuffEffect(ASC, Handle, -1));
    TestFalse(TEXT("移除全部层数后 Buff 不再激活"), UGameGasBuffBridge::IsBuffEffectActive(ASC, Handle));
    TestEqual(TEXT("移除后叠层数为 0"), UGameGasBuffBridge::GetBuffStackCount(ASC, Handle), 0);

    BaseCDO->DurationPolicy = EGameplayEffectDurationType::HasDuration;
    BaseCDO->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(10.0f));

    const FActiveGameplayEffectHandle DurationHandle = UGameGasBuffBridge::ApplyBuffEffect(ASC, UGameplayEffect::StaticClass(), 1, nullptr);
    TestTrue(TEXT("施加有限时长 Buff 成功且 Source 为空安全通过"), DurationHandle.IsValid());
    TestEqual(TEXT("有限时长 Buff 总时长为 10.0"), UGameGasBuffBridge::GetBuffDuration(ASC, DurationHandle), 10.0f);
    TestTrue(TEXT("有限时长 Buff 剩余时间大于 0"), UGameGasBuffBridge::GetBuffRemainingTime(ASC, DurationHandle) > 0.0f);
    TestTrue(TEXT("有限时长 Buff 剩余时间不大于总时长"), UGameGasBuffBridge::GetBuffRemainingTime(ASC, DurationHandle) <= 10.0f);
    TestTrue(TEXT("移除有限时长 Buff 成功"), UGameGasBuffBridge::RemoveBuffEffect(ASC, DurationHandle));

    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    BaseCDO->DurationPolicy = OldDurationPolicy;
    BaseCDO->DurationMagnitude = OldDurationMagnitude;
    BaseCDO->StackingType = OldStackingType;
    BaseCDO->StackLimitCount = OldStackLimitCount;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
