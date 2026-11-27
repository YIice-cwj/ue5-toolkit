/**
 * @file GameFeedbackAdapterTest.cpp
 * @brief 战斗伤害上下文到打击感信号适配器自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Bridge/UGameFeedbackAdapter.h"
#include "Damage/FGameDamageChannel.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Engine/World.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayFeedbackAdapterTest,
    "Game.Gameplay.FeedbackAdapter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证伤害上下文与结算结果到表现信号的字段适配、主通道选取、致死状态与目标回退逻辑
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayFeedbackAdapterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    AActor* Instigator = World->SpawnActor<AActor>();
    AActor* DamageCauser = World->SpawnActor<AActor>();
    AActor* Target = World->SpawnActor<AActor>();

    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Adapter.Slash")));
    const FGameplayTag BluntTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Adapter.Blunt")));

    FGameDamageContext Context;
    Context.Instigator = Instigator;
    Context.DamageCauser = DamageCauser;
    Context.HitResult.ImpactNormal = FVector::UpVector;
    Context.HitResult.ImpactPoint = FVector(10.0f, 20.0f, 30.0f);
    Context.HitResult.TraceStart = FVector::ZeroVector;
    Context.HitResult.TraceEnd = FVector(10.0f, 0.0f, 0.0f);

    FGameDamageResult Result;
    Result.SetActualDamage(150.0f);
    FGameDamageChannel::SetExact(Result.ActualChannels, SlashTag, 100.0f);
    FGameDamageChannel::SetExact(Result.ActualChannels, BluntTag, 50.0f);
    Result.AddFeedbackTag(TAG_Feedback_State_Lethal);

    FGameFeedbackSignal Signal = UGameFeedbackAdapter::MakeSignalFromDamage(Target, Context, Result);
    TestEqual(TEXT("显式传入 Target 必须准确绑定"), Signal.Target.Get(), Target);
    TestEqual(TEXT("Instigator 必须准确绑定"), Signal.Instigator.Get(), Instigator);
    TestEqual(TEXT("DamageCauser 必须准确绑定"), Signal.DamageCauser.Get(), DamageCauser);
    TestEqual(TEXT("主导通道应选择实际伤害最大的通道 (Slash)"), Signal.PrimaryChannel, SlashTag);
    TestEqual(TEXT("伤害总值应当精确传递"), Signal.Magnitude, 150.0f);
    TestTrue(TEXT("包含致死标签时 bLethal 必须为 true"), Signal.bLethal);
    TestEqual(TEXT("无速度 DamageCauser 回落至碰撞法线作为冲击方向"), Signal.ImpactDirection, FVector::UpVector);
    TestEqual(TEXT("轨迹方向应按 TraceEnd - TraceStart 归一化保留"), Signal.TraceDirection, FVector::ForwardVector);

    Context.DamageCauser = nullptr;
    Context.HitResult = FHitResult(Target, nullptr, FVector::ZeroVector, FVector::RightVector);
    Result.FeedbackTags.RemoveTag(TAG_Feedback_State_Lethal);
    Signal = UGameFeedbackAdapter::MakeSignalFromDamage(nullptr, Context, Result);
    TestEqual(TEXT("缺省 Target 时应从 HitResult 提取目标"), Signal.Target.Get(), Target);
    TestEqual(TEXT("空 DamageCauser 应使用 ImpactNormal"), Signal.ImpactDirection, FVector::RightVector);
    TestFalse(TEXT("无致死标签时 bLethal 必须为 false"), Signal.bLethal);

    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
