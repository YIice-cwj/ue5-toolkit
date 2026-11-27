// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "GameFramework/Actor.h"
#include "Tests/GameFeedbackTestHelpers.h"
#include "Time/FActorHitStopTable.h"
#include "Time/FGlobalDilationMixer.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameTimeHitStopTableTest,
    "Game.Feedback.Time.HitStopTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief Actor 局部顿帧表核心功能与边界测试（时长/缩放合并、非正时长拦截、到期还原与销毁自愈）
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameTimeHitStopTableTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* ActorA = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
    AActor* ActorB = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
    TestNotNull(TEXT("目标 A 生成成功"), ActorA);
    TestNotNull(TEXT("目标 B 生成成功"), ActorB);
    if (!ActorA || !ActorB) {
        return false;
    }

    ActorA->CustomTimeDilation = 1.0f;
    ActorB->CustomTimeDilation = 1.0f;

    FActorHitStopTable Table;
    TestFalse(TEXT("初始无活跃顿帧记录"), Table.HasActive());

    Table.Request(*ActorA, 0.0f, 0.05f);
    Table.Request(*ActorA, -0.5f, 0.05f);
    Table.Request(*ActorA, std::numeric_limits<float>::quiet_NaN(), 0.05f);
    TestFalse(TEXT("非正或 NaN 时长请求必须忽略且不激活记录"), Table.IsActive(ActorA));
    TestNearlyEqual(TEXT("非正或 NaN 时长请求不得修改目标缩放"), ActorA->CustomTimeDilation, 1.0f, 0.0001f);

    Table.Request(*ActorA, 0.2f, 0.05f);
    TestTrue(TEXT("请求后 HasActive 返回 true"), Table.HasActive());
    TestNearlyEqual(TEXT("顿帧缩放已应用"), ActorA->CustomTimeDilation, 0.05f, 0.0001f);
    TestTrue(TEXT("顿帧记录已建立"), Table.IsActive(ActorA));

    Table.Request(*ActorA, 0.1f, 0.5f);
    TestNearlyEqual(TEXT("重复请求取较小缩放"), ActorA->CustomTimeDilation, 0.05f, 0.0001f);

    Table.Advance(-0.5f);
    TestTrue(TEXT("负数时间推进应安全忽略"), Table.IsActive(ActorA));

    Table.Advance(0.2f);
    TestNearlyEqual(TEXT("到期后还原原始缩放"), ActorA->CustomTimeDilation, 1.0f, 0.0001f);
    TestFalse(TEXT("到期后记录移除"), Table.IsActive(ActorA));
    TestFalse(TEXT("全部到期后 HasActive 为 false"), Table.HasActive());

    Table.Request(*ActorB, 0.1f, 0.2f);
    TestNearlyEqual(TEXT("第二目标缩放已应用"), ActorB->CustomTimeDilation, 0.2f, 0.0001f);
    Table.Clear(ActorB);
    TestNearlyEqual(TEXT("主动清除立即还原"), ActorB->CustomTimeDilation, 1.0f, 0.0001f);
    TestFalse(TEXT("主动清除后记录移除"), Table.IsActive(ActorB));

    Table.Request(*ActorB, 0.1f, 0.2f);
    Table.Request(*ActorA, 0.1f, 0.2f);
    Table.ClearAll();
    TestNearlyEqual(TEXT("全部清除还原 A"), ActorA->CustomTimeDilation, 1.0f, 0.0001f);
    TestNearlyEqual(TEXT("全部清除还原 B"), ActorB->CustomTimeDilation, 1.0f, 0.0001f);
    TestFalse(TEXT("全部清除后无激活记录"), Table.IsActive(ActorA));

    AActor* ActorC = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
    if (ActorC != nullptr) {
        Table.Request(*ActorC, 0.5f, 0.05f);
        TestTrue(TEXT("ActorC 建立顿帧记录"), Table.IsActive(ActorC));
        ActorC->Destroy();
        Table.Advance(0.1f);
        TestFalse(TEXT("销毁后推进步进自愈清理失效弱引用"), Table.IsActive(ActorC));
        Table.PruneInvalid();
    }

    TArray<AActor*> DeadBatch;
    DeadBatch.Reserve(32);
    for (int32 Index = 0; Index < 32; ++Index) {
        if (AActor* DeadItem = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams)) {
            Table.Request(*DeadItem, 1.0f, 0.1f);
            DeadBatch.Add(DeadItem);
        }
    }
    for (AActor* DeadItem : DeadBatch) {
        DeadItem->Destroy();
    }
    for (int32 Index = 0; Index < DeadBatch.Num() / 2; ++Index) {
        Table.Clear(DeadBatch[Index]);
    }
    for (int32 Index = DeadBatch.Num() / 2; Index < DeadBatch.Num(); ++Index) {
        TestTrue(TEXT("批量销毁哈希桶碰撞下未 Clear 的失效 Actor 键保持独立存在"), Table.IsActive(DeadBatch[Index]));
    }
    Table.PruneInvalid();
    for (AActor* DeadItem : DeadBatch) {
        TestFalse(TEXT("PruneInvalid 后全部失效 Actor 记录均已清理"), Table.IsActive(DeadItem));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameTimeDilationMixerTest,
    "Game.Feedback.Time.DilationMixer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 全局分层时间膨胀混音器测试（静态零堆通道优先级覆盖、负数防御与平滑淡出）
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameTimeDilationMixerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGlobalDilationMixer Mixer;

    TestNearlyEqual(TEXT("初始生效值为 1.0"), Mixer.GetEffectiveDilation(), 1.0f, 0.0001f);
    TestFalse(TEXT("初始 HasActive 为 false"), Mixer.HasActive());
    TestFalse(TEXT("初始无激活通道"), Mixer.IsActive(ETimeDilationChannel::SlowMo));

    Mixer.Request(ETimeDilationChannel::SlowMo, 1.0f, 0.5f, 0.0f);
    TestTrue(TEXT("请求后 HasActive 为 true"), Mixer.HasActive());
    TestTrue(TEXT("请求后通道激活"), Mixer.IsActive(ETimeDilationChannel::SlowMo));
    TestNearlyEqual(TEXT("生效值取激活通道缩放"), Mixer.GetEffectiveDilation(), 0.5f, 0.0001f);
    TestNearlyEqual(TEXT("未激活通道返回 1.0"), Mixer.GetChannelDilation(ETimeDilationChannel::HitStop), 1.0f, 0.0001f);

    Mixer.Advance(-2.0f);
    TestNearlyEqual(TEXT("负数步进应被忽略不得拉长持续时长"), Mixer.GetEffectiveDilation(), 0.5f, 0.0001f);

    Mixer.Request(ETimeDilationChannel::HitStop, 0.5f, 0.1f, 0.0f);
    TestNearlyEqual(TEXT("高优先级通道完全覆盖"), Mixer.GetEffectiveDilation(), 0.1f, 0.0001f);

    Mixer.Clear(ETimeDilationChannel::HitStop);
    TestNearlyEqual(TEXT("清除高优先级后回落到低优先级"), Mixer.GetEffectiveDilation(), 0.5f, 0.0001f);

    Mixer.Advance(0.5f);
    TestTrue(TEXT("第一次推进未到期"), Mixer.IsActive(ETimeDilationChannel::SlowMo));
    Mixer.Advance(0.5f);
    TestFalse(TEXT("时长耗尽自动清除"), Mixer.IsActive(ETimeDilationChannel::SlowMo));
    TestFalse(TEXT("全部通道结束后 HasActive 为 false"), Mixer.HasActive());
    TestNearlyEqual(TEXT("到期后生效值恢复 1.0"), Mixer.GetEffectiveDilation(), 1.0f, 0.0001f);

    Mixer.Request(ETimeDilationChannel::WorldModifier, 0.2f, 0.25f, 0.3f);
    Mixer.Advance(0.2f);
    TestTrue(TEXT("进入淡出阶段仍激活"), Mixer.IsActive(ETimeDilationChannel::WorldModifier));
    Mixer.Advance(0.15f);
    TestNearlyEqual(TEXT("淡出中途按比例插值"), Mixer.GetEffectiveDilation(), FMath::Lerp(0.25f, 1.0f, 0.5f), 0.0001f);
    Mixer.Advance(0.15f);
    TestFalse(TEXT("淡出结束自动清除"), Mixer.IsActive(ETimeDilationChannel::WorldModifier));
    TestNearlyEqual(TEXT("淡出结束生效值恢复 1.0"), Mixer.GetEffectiveDilation(), 1.0f, 0.0001f);

    Mixer.Request(ETimeDilationChannel::SystemPause, 0.0f, 0.0f, 0.0f);
    TestTrue(TEXT("零时长通道持续激活"), Mixer.IsActive(ETimeDilationChannel::SystemPause));
    TestNearlyEqual(TEXT("零时间缩放安全钳制为 0.0001f"), Mixer.GetEffectiveDilation(), 0.0001f, 0.00001f);

    Mixer.ClearAll();
    TestNearlyEqual(TEXT("全部清除后恢复 1.0"), Mixer.GetEffectiveDilation(), 1.0f, 0.0001f);
    TestFalse(TEXT("全部清除后 HasActive 为 false"), Mixer.HasActive());
    TestFalse(TEXT("全部清除后无激活通道"), Mixer.IsActive(ETimeDilationChannel::SystemPause));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
