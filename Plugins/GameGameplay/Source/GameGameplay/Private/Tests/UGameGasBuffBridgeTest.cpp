/**
 * @file UGameGasBuffBridgeTest.cpp
 * @brief GAS Buff 桥接层（同体/异体施加、叠层、无限/有限时长查询与 const 只读契约）自动化测试套件
 */

#include "Bridge/UGameGasBuffBridge.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGasBuffBridgeTest,
    "Game.Gameplay.GasBuffBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GAS Buff 施加、异体施法者归属、叠层扣减、无限/有限时长与 const 只读查询安全性
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
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
    ON_SCOPE_EXIT {
        World->DestroyWorld(false);
    };

    AActor* TargetActor = World->SpawnActor<AActor>();
    UGameAbilitySystemComponent* TargetASC = NewObject<UGameAbilitySystemComponent>(TargetActor);
    TargetASC->RegisterComponent();
    TargetASC->InitAbilityActorInfo(TargetActor, TargetActor);

    AActor* SourceActor = World->SpawnActor<AActor>();
    UGameAbilitySystemComponent* SourceASC = NewObject<UGameAbilitySystemComponent>(SourceActor);
    SourceASC->RegisterComponent();
    SourceASC->InitAbilityActorInfo(SourceActor, SourceActor);

    const UAbilitySystemComponent* ConstTargetASC = TargetASC;

    TestFalse(TEXT("有效 ASC 但空效果类施加失败"), UGameGasBuffBridge::ApplyBuffEffect(TargetASC, nullptr).IsValid());
    TestFalse(TEXT("无效句柄查询激活失败"), UGameGasBuffBridge::IsBuffEffectActive(ConstTargetASC, InvalidHandle));

    const FActiveGameplayEffectHandle NonExistentHandle = FActiveGameplayEffectHandle::GenerateNewHandle(TargetASC);
    TestFalse(TEXT("有效 ASC 但不存在句柄查询激活返回 false"), UGameGasBuffBridge::IsBuffEffectActive(ConstTargetASC, NonExistentHandle));
    TestEqual(TEXT("有效 ASC 但不存在句柄查询叠层返回 0"), UGameGasBuffBridge::GetBuffStackCount(ConstTargetASC, NonExistentHandle), 0);
    TestEqual(TEXT("有效 ASC 但不存在句柄查询剩余时间返回 0.0"), UGameGasBuffBridge::GetBuffRemainingTime(ConstTargetASC, NonExistentHandle), 0.0f);
    TestEqual(TEXT("有效 ASC 但不存在句柄查询总时长返回 0.0"), UGameGasBuffBridge::GetBuffDuration(ConstTargetASC, NonExistentHandle), 0.0f);
    TestFalse(TEXT("有效 ASC 但不存在句柄移除返回 false"), UGameGasBuffBridge::RemoveBuffEffect(TargetASC, NonExistentHandle));

    UGameplayEffect* BaseCDO = GetMutableDefault<UGameplayEffect>();
    const EGameplayEffectDurationType OldDurationPolicy = BaseCDO->DurationPolicy;
    const FGameplayEffectModifierMagnitude OldDurationMagnitude = BaseCDO->DurationMagnitude;
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    const EGameplayEffectStackingType OldStackingType = BaseCDO->StackingType;
    const int32 OldStackLimitCount = BaseCDO->StackLimitCount;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

    ON_SCOPE_EXIT {
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        BaseCDO->DurationPolicy = OldDurationPolicy;
        BaseCDO->DurationMagnitude = OldDurationMagnitude;
        BaseCDO->StackingType = OldStackingType;
        BaseCDO->StackLimitCount = OldStackLimitCount;
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
    };

    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    BaseCDO->DurationPolicy = EGameplayEffectDurationType::Infinite;
    BaseCDO->StackingType = EGameplayEffectStackingType::AggregateByTarget;
    BaseCDO->StackLimitCount = 5;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

    const FActiveGameplayEffectHandle Handle = UGameGasBuffBridge::ApplyBuffEffect(TargetASC, UGameplayEffect::StaticClass(), 1, TargetActor);
    TestTrue(TEXT("施加无限时长 Buff 成功且返回有效句柄"), Handle.IsValid());
    TestTrue(TEXT("const ASC 查询 Buff 处于激活状态"), UGameGasBuffBridge::IsBuffEffectActive(ConstTargetASC, Handle));
    TestEqual(TEXT("首次施加叠层数为 1"), UGameGasBuffBridge::GetBuffStackCount(ConstTargetASC, Handle), 1);
    TestEqual(TEXT("无限时长 Buff 总时长返回 -1.0"), UGameGasBuffBridge::GetBuffDuration(ConstTargetASC, Handle), -1.0f);
    TestEqual(TEXT("无限时长 Buff 剩余时长返回 -1.0"), UGameGasBuffBridge::GetBuffRemainingTime(ConstTargetASC, Handle), -1.0f);

    UGameAbilitySystemComponent* UninitializedASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    TestFalse(
        TEXT("尚未 InitAbilityActorInfo 的 ASC 施加 Buff 安全拒绝且不崩溃"),
        UGameGasBuffBridge::ApplyBuffEffect(UninitializedASC, UGameplayEffect::StaticClass(), 1).IsValid()
    );

    AActor* WorldlessActor = NewObject<AActor>(GetTransientPackage());
    UGameAbilitySystemComponent* WorldlessASC = NewObject<UGameAbilitySystemComponent>(WorldlessActor);
    WorldlessASC->InitAbilityActorInfo(WorldlessActor, WorldlessActor);
    TestFalse(
        TEXT("缺少有效 World 的 ASC 施加 Buff 安全拒绝且不触发引擎 GetWorldTime 空解引用崩溃"),
        UGameGasBuffBridge::ApplyBuffEffect(WorldlessASC, UGameplayEffect::StaticClass(), 1).IsValid()
    );

    const FActiveGameplayEffectHandle SecondHandle = UGameGasBuffBridge::ApplyBuffEffect(TargetASC, UGameplayEffect::StaticClass(), 1, TargetActor);
    TestTrue(TEXT("再次施加叠层成功"), SecondHandle.IsValid());
    TestEqual(TEXT("叠层数自增为 2"), UGameGasBuffBridge::GetBuffStackCount(ConstTargetASC, Handle), 2);

    TestTrue(TEXT("成功移除 1 层 Buff"), UGameGasBuffBridge::RemoveBuffEffect(TargetASC, Handle, 1));
    TestEqual(TEXT("扣减后剩余 1 层"), UGameGasBuffBridge::GetBuffStackCount(ConstTargetASC, Handle), 1);
    TestTrue(TEXT("扣减 1 层后 Buff 仍然激活"), UGameGasBuffBridge::IsBuffEffectActive(ConstTargetASC, Handle));

    TestTrue(TEXT("全部移除 Buff 成功"), UGameGasBuffBridge::RemoveBuffEffect(TargetASC, Handle, -1));
    TestFalse(TEXT("移除全部层数后 Buff 不再激活"), UGameGasBuffBridge::IsBuffEffectActive(ConstTargetASC, Handle));
    TestEqual(TEXT("移除后叠层数为 0"), UGameGasBuffBridge::GetBuffStackCount(ConstTargetASC, Handle), 0);

    const FActiveGameplayEffectHandle RemoteHandle = UGameGasBuffBridge::ApplyBuffEffect(
        TargetASC,
        UGameplayEffect::StaticClass(),
        2,
        SourceActor,
        SourceASC
    );
    TestTrue(TEXT("异体 SourceASC -> TargetASC 施加 Buff 成功"), RemoteHandle.IsValid());
    const FActiveGameplayEffect* RemoteActiveGE = TargetASC->GetActiveGameplayEffect(RemoteHandle);
    TestNotNull(TEXT("目标 ASC 查到异体施加的 ActiveGE"), RemoteActiveGE);
    if (RemoteActiveGE != nullptr) {
        TestEqual(
            TEXT("异体施加 Buff 的来源 ASC 正确归属施法者而非目标自身"),
            RemoteActiveGE->Spec.GetContext().GetInstigatorAbilitySystemComponent(),
            static_cast<UAbilitySystemComponent*>(SourceASC)
        );
        TestEqual(TEXT("异体施加 Buff 等级透传正确"), RemoteActiveGE->Spec.GetLevel(), 2.0f);
    }
    TestTrue(TEXT("移除异体 Buff 成功"), UGameGasBuffBridge::RemoveBuffEffect(TargetASC, RemoteHandle));

    UGameAbilitySystemComponent* StaleSourceASC = NewObject<UGameAbilitySystemComponent>(SourceActor);
    StaleSourceASC->MarkAsGarbage();
    TestFalse(
        TEXT("显式传入已失效的非空施法者 ASC 必须拒绝施加而不回退自施加"),
        UGameGasBuffBridge::ApplyBuffEffect(TargetASC, UGameplayEffect::StaticClass(), 1, SourceActor, StaleSourceASC).IsValid()
    );

    BaseCDO->DurationPolicy = EGameplayEffectDurationType::HasDuration;
    BaseCDO->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(10.0f));

    const FActiveGameplayEffectHandle DurationHandle = UGameGasBuffBridge::ApplyBuffEffect(TargetASC, UGameplayEffect::StaticClass(), 1, nullptr);
    TestTrue(TEXT("施加有限时长 Buff 成功且 Source 为空安全通过"), DurationHandle.IsValid());
    TestEqual(TEXT("有限时长 Buff 总时长为 10.0"), UGameGasBuffBridge::GetBuffDuration(ConstTargetASC, DurationHandle), 10.0f);
    TestTrue(TEXT("有限时长 Buff 剩余时间大于 0"), UGameGasBuffBridge::GetBuffRemainingTime(ConstTargetASC, DurationHandle) > 0.0f);
    TestTrue(TEXT("有限时长 Buff 剩余时间不大于总时长"), UGameGasBuffBridge::GetBuffRemainingTime(ConstTargetASC, DurationHandle) <= 10.0f);
    TestTrue(TEXT("移除有限时长 Buff 成功"), UGameGasBuffBridge::RemoveBuffEffect(TargetASC, DurationHandle));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
