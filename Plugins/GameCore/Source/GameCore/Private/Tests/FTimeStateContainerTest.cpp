#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Tests/GamePoolableTestActor.h"
#include "Time/FActorHitStopTable.h"
#include "Time/FGlobalDilationMixer.h"

#if WITH_AUTOMATION_TESTS

namespace {

UWorld* GetTimeStateTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World()) {
            return Context.World();
        }
    }
    return nullptr;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameTimeHitStopTableTest,
    "Game.Core.Time.HitStopTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief Actor 局部顿帧表核心功能测试（时长与缩放合并、到期还原与清除）
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameTimeHitStopTableTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTimeStateTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AGamePoolableTestActor* ActorA = World->SpawnActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(), FTransform::Identity, SpawnParams);
    AGamePoolableTestActor* ActorB = World->SpawnActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(), FTransform::Identity, SpawnParams);
    TestNotNull(TEXT("目标 A 生成成功"), ActorA);
    TestNotNull(TEXT("目标 B 生成成功"), ActorB);
    if (!ActorA || !ActorB) {
        return false;
    }

    ActorA->CustomTimeDilation = 1.0f;
    ActorB->CustomTimeDilation = 1.0f;

    FActorHitStopTable Table;
    Table.Request(*ActorA, 0.2f, 0.05f);
    TestNearlyEqual(TEXT("顿帧缩放已应用"), ActorA->CustomTimeDilation, 0.05f, 0.0001f);
    TestTrue(TEXT("顿帧记录已建立"), Table.IsActive(ActorA));

    Table.Request(*ActorA, 0.1f, 0.5f);
    TestNearlyEqual(TEXT("重复请求取较小缩放"), ActorA->CustomTimeDilation, 0.05f, 0.0001f);
    TestNearlyEqual(TEXT("重复请求不改写原始缩放记录"), ActorA->CustomTimeDilation, 0.05f, 0.0001f);

    Table.Advance(0.2f);
    TestNearlyEqual(TEXT("到期后还原原始缩放"), ActorA->CustomTimeDilation, 1.0f, 0.0001f);
    TestFalse(TEXT("到期后记录移除"), Table.IsActive(ActorA));

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

    AGamePoolableTestActor* ActorC = World->SpawnActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(), FTransform::Identity, SpawnParams);
    if (ActorC != nullptr) {
        Table.Request(*ActorC, 0.5f, 0.05f);
        TestTrue(TEXT("ActorC 建立顿帧记录"), Table.IsActive(ActorC));
        ActorC->Destroy();
        Table.Advance(0.1f);
        TestFalse(TEXT("销毁后推进步进自愈清理失效弱引用"), Table.IsActive(ActorC));
        Table.PruneInvalid();
    }

    ActorA->Destroy();
    ActorB->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameTimeDilationMixerTest,
    "Game.Core.Time.DilationMixer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 全局分层时间膨胀混音器测试（通道优先级覆盖与生效计算）
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameTimeDilationMixerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGlobalDilationMixer Mixer;

    TestNearlyEqual(TEXT("初始生效值为 1.0"), Mixer.GetEffectiveDilation(), 1.0f, 0.0001f);
    TestFalse(TEXT("初始无激活通道"), Mixer.IsActive(ETimeDilationChannel::SlowMo));

    Mixer.Request(ETimeDilationChannel::SlowMo, 1.0f, 0.5f, 0.0f);
    TestTrue(TEXT("请求后通道激活"), Mixer.IsActive(ETimeDilationChannel::SlowMo));
    TestNearlyEqual(TEXT("生效值取激活通道缩放"), Mixer.GetEffectiveDilation(), 0.5f, 0.0001f);
    TestNearlyEqual(TEXT("未激活通道返回 1.0"), Mixer.GetChannelDilation(ETimeDilationChannel::HitStop), 1.0f, 0.0001f);

    Mixer.Request(ETimeDilationChannel::HitStop, 0.5f, 0.1f, 0.0f);
    TestNearlyEqual(TEXT("高优先级通道完全覆盖"), Mixer.GetEffectiveDilation(), 0.1f, 0.0001f);

    Mixer.Clear(ETimeDilationChannel::HitStop);
    TestNearlyEqual(TEXT("清除高优先级后回落到低优先级"), Mixer.GetEffectiveDilation(), 0.5f, 0.0001f);

    Mixer.Advance(0.5f);
    TestTrue(TEXT("第一次推进未到期"), Mixer.IsActive(ETimeDilationChannel::SlowMo));
    Mixer.Advance(0.5f);
    TestFalse(TEXT("时长耗尽自动清除"), Mixer.IsActive(ETimeDilationChannel::SlowMo));
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
    TestNearlyEqual(TEXT("零时间缩放立即生效"), Mixer.GetEffectiveDilation(), 0.0f, 0.0001f);

    Mixer.ClearAll();
    TestNearlyEqual(TEXT("全部清除后恢复 1.0"), Mixer.GetEffectiveDilation(), 1.0f, 0.0001f);
    TestFalse(TEXT("全部清除后无激活通道"), Mixer.IsActive(ETimeDilationChannel::SystemPause));

    return true;
}

#endif
