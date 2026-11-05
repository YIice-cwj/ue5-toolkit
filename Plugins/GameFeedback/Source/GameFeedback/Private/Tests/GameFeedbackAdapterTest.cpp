/**
 * @file GameFeedbackAdapterTest.cpp
 * @brief 伤害上下文适配器与蓝图函数库门禁自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Adapter/GameFeedbackAdapter.h"
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Config/GameFeedbackSettings.h"
#include "Damage/FGameDamageChannel.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Engine/World.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackAdapterTest,
    "Game.Feedback.AdapterConversion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证伤害上下文与结果到表现信号的字段适配与主通道选取
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackAdapterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDamageContext Context;
    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Channel.Slash")));
    const FGameplayTag BluntTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Channel.Blunt")));

    Context.HitResult.ImpactNormal = FVector::UpVector;
    Context.HitResult.ImpactPoint = FVector(10.0f, 20.0f, 30.0f);
    Context.HitResult.TraceStart = FVector::ZeroVector;
    Context.HitResult.TraceEnd = FVector(10.0f, 0.0f, 0.0f);

    FGameDamageResult Result;
    Result.SetActualDamage(150.0f);
    FGameDamageChannel::SetExact(Result.ActualChannels, SlashTag, 100.0f);
    FGameDamageChannel::SetExact(Result.ActualChannels, BluntTag, 50.0f);
    Result.AddFeedbackTag(TAG_Feedback_State_Lethal);

    FGameFeedbackSignal Signal = UGameFeedbackAdapter::MakeSignalFromDamage(nullptr, Context, Result);

    TestEqual(TEXT("主导通道应选择伤害最大的通道 (Slash)"), Signal.PrimaryChannel, SlashTag);
    TestEqual(TEXT("伤害总值应当正确传递"), Signal.Magnitude, 150.0f);
    TestTrue(TEXT("包含致死标签时 bLethal 必须为 true"), Signal.bLethal);
    TestEqual(TEXT("冲击法线应正确对齐"), Signal.HitResult.ImpactNormal, FVector::UpVector);
    TestEqual(TEXT("轨迹方向应显式保留"), Signal.TraceDirection, FVector::ForwardVector);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackAdapterAndLibraryTest,
    "Game.Feedback.AdapterAndLibrary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Adapter 伤害上下文提取与 BlueprintLibrary 广播开关门禁
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackAdapterAndLibraryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AActor* Instigator = World->SpawnActor<AActor>();
    AActor* DamageCauser = World->SpawnActor<AActor>();
    AActor* Target = World->SpawnActor<AActor>();

    FGameDamageContext Context;
    Context.Instigator = Instigator;
    Context.DamageCauser = DamageCauser;
    Context.HitResult.ImpactNormal = FVector::UpVector;
    Context.HitResult.TraceStart = FVector(0.0f, 0.0f, 100.0f);
    Context.HitResult.TraceEnd = FVector::ZeroVector;

    FGameDamageResult Result;
    Result.TotalActualDamage = 25.0f;
    Result.FeedbackTags.AddTag(TAG_Feedback_State_Lethal);

    FGameFeedbackSignal Signal = UGameFeedbackAdapter::MakeSignalFromDamage(Target, Context, Result);
    TestEqual(TEXT("显式传入 Target 必须有效绑定"), Signal.Target.Get(), Target);
    TestTrue(TEXT("Lethal 标签应被解析为真"), Signal.bLethal);
    TestEqual(TEXT("伤害数值必须精确传递"), Signal.Magnitude, 25.0f);
    TestEqual(TEXT("碰撞法线作为默认冲击方向"), Signal.ImpactDirection, FVector::UpVector);

    Context.HitResult = FHitResult(Target, nullptr, FVector::ZeroVector, FVector::UpVector);
    Signal = UGameFeedbackAdapter::MakeSignalFromDamage(nullptr, Context, Result);
    TestEqual(TEXT("缺省 Target 时由 HitResult 提取目标"), Signal.Target.Get(), Target);

    UGameFeedbackSettings* Settings = GetMutableDefault<UGameFeedbackSettings>();
    const bool bPrevEnable = Settings->bEnableFeedback;

    Settings->bEnableFeedback = false;
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, Signal);
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, Signal, nullptr);
    UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(World, Target);
    UGameFeedbackBlueprintLibrary::BroadcastNetworkFeedback(World, Signal, nullptr, false);

    Settings->bEnableFeedback = bPrevEnable;
    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
