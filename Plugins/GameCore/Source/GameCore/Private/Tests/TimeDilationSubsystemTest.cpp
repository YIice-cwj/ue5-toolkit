// Copyright Epic Games, Inc. All Rights Reserved.

#include "Time/TimeDilationSubsystem.h"
#include "Tests/GamePoolableTestActor.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

namespace {

UWorld* GetTimeDilationTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World()) {
            return Context.World();
        }
    }
    return nullptr;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTimeActorHitStopTest, "Game.Core.Time.ActorHitStop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证 Actor 打击顿帧申请、定时步进、自定义缩放还原与即时清理
 */
bool FGameTimeActorHitStopTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTimeDilationTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UTimeDilationSubsystem* TimeSubsystem = World->GetSubsystem<UTimeDilationSubsystem>();
    TestNotNull(TEXT("时间膨胀子系统有效"), TimeSubsystem);
    if (!TimeSubsystem) {
        return false;
    }

    TimeSubsystem->ClearAllHitStops();

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AGamePoolableTestActor* TargetActor = World->SpawnActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass(), FTransform::Identity, SpawnParams);
    TestNotNull(TEXT("测试 Actor 生成成功"), TargetActor);
    if (!TargetActor) {
        return false;
    }

    TargetActor->CustomTimeDilation = 1.0f;

    TimeSubsystem->RequestHitStop(TargetActor, 0.1f, 0.05f);
    TestTrue(TEXT("Actor 正处于顿帧中"), TimeSubsystem->IsActorInHitStop(TargetActor));
    TestNearlyEqual(TEXT("Actor 时间缩放已设定为顿帧值"), TargetActor->CustomTimeDilation, 0.05f, 0.0001f);

    TimeSubsystem->AdvanceTime(0.05f);
    TestTrue(TEXT("未满持续时间前依然处于顿帧"), TimeSubsystem->IsActorInHitStop(TargetActor));
    TestNearlyEqual(TEXT("时间缩放保持顿帧值"), TargetActor->CustomTimeDilation, 0.05f, 0.0001f);

    TimeSubsystem->AdvanceTime(0.06f);
    TestFalse(TEXT("超过持续时间后退出顿帧"), TimeSubsystem->IsActorInHitStop(TargetActor));
    TestNearlyEqual(TEXT("Actor 时间缩放自动还原"), TargetActor->CustomTimeDilation, 1.0f, 0.0001f);

    TimeSubsystem->RequestHitStop(TargetActor, 0.5f, 0.01f);
    TestTrue(TEXT("再次请求顿帧生效"), TimeSubsystem->IsActorInHitStop(TargetActor));
    TimeSubsystem->ClearHitStop(TargetActor);
    TestFalse(TEXT("主动清除后立即退出顿帧"), TimeSubsystem->IsActorInHitStop(TargetActor));
    TestNearlyEqual(TEXT("主动清除后时间缩放立即还原"), TargetActor->CustomTimeDilation, 1.0f, 0.0001f);

    TargetActor->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTimePriorityStackTest, "Game.Core.Time.LayeredPriorityStack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证分层时间膨胀通道优先级覆盖与退栈还原机制
 */
bool FGameTimePriorityStackTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTimeDilationTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UTimeDilationSubsystem* TimeSubsystem = World->GetSubsystem<UTimeDilationSubsystem>();
    TestNotNull(TEXT("时间膨胀子系统有效"), TimeSubsystem);
    if (!TimeSubsystem) {
        return false;
    }

    TimeSubsystem->ClearAllGlobalDilations();
    TestNearlyEqual(TEXT("初始状态全局时间缩放为 1.0"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 1.0f, 0.0001f);

    TimeSubsystem->RequestGlobalDilation(ETimeDilationChannel::WorldModifier, 10.0f, 0.8f);
    TestNearlyEqual(TEXT("激活 WorldModifier 通道"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.8f, 0.0001f);
    TestNearlyEqual(TEXT("读取指定通道时间膨胀值"), TimeSubsystem->GetChannelTimeDilation(ETimeDilationChannel::WorldModifier), 0.8f, 0.0001f);
    TestNearlyEqual(TEXT("未激活通道时间膨胀默认为 1.0"), TimeSubsystem->GetChannelTimeDilation(ETimeDilationChannel::SlowMo), 1.0f, 0.0001f);

    TimeSubsystem->RequestGlobalDilation(ETimeDilationChannel::SlowMo, 2.0f, 0.3f);
    TestNearlyEqual(TEXT("SlowMo 优先级高于 WorldModifier"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.3f, 0.0001f);

    TimeSubsystem->RequestGlobalDilation(ETimeDilationChannel::HitStop, 0.1f, 0.01f);
    TestNearlyEqual(TEXT("HitStop 优先级高于 SlowMo"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.01f, 0.0001f);

    TimeSubsystem->AdvanceTime(0.15f);
    TestFalse(TEXT("HitStop 到期自动退栈"), TimeSubsystem->IsChannelActive(ETimeDilationChannel::HitStop));
    TestNearlyEqual(TEXT("退栈后回落至 SlowMo"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.3f, 0.0001f);

    TimeSubsystem->RequestGlobalDilation(ETimeDilationChannel::SystemPause, 0.0f, 0.0f);
    TestNearlyEqual(TEXT("SystemPause 拥有最高优先级"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.0f, 0.0001f);

    TimeSubsystem->ClearGlobalDilation(ETimeDilationChannel::SystemPause);
    TestNearlyEqual(TEXT("解除暂停后回落至 SlowMo"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.3f, 0.0001f);

    TimeSubsystem->AdvanceTime(3.0f);
    TestFalse(TEXT("SlowMo 到期自动退栈"), TimeSubsystem->IsChannelActive(ETimeDilationChannel::SlowMo));
    TestNearlyEqual(TEXT("回落至底层 WorldModifier"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.8f, 0.0001f);

    TimeSubsystem->ClearGlobalDilation(ETimeDilationChannel::WorldModifier);
    TestNearlyEqual(TEXT("清空全部通道后恢复默认 1.0"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 1.0f, 0.0001f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTimeBlendOutRecoveryTest, "Game.Core.Time.BlendOutRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证时间膨胀平滑插值淡出回弹曲线
 */
bool FGameTimeBlendOutRecoveryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTimeDilationTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UTimeDilationSubsystem* TimeSubsystem = World->GetSubsystem<UTimeDilationSubsystem>();
    TestNotNull(TEXT("时间膨胀子系统有效"), TimeSubsystem);
    if (!TimeSubsystem) {
        return false;
    }

    TimeSubsystem->ClearAllGlobalDilations();

    TimeSubsystem->RequestGlobalDilation(ETimeDilationChannel::SlowMo, 1.0f, 0.2f, 1.0f);
    TestNearlyEqual(TEXT("慢放初始时间缩放为 0.2"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.2f, 0.0001f);

    TimeSubsystem->AdvanceTime(0.5f);
    TestNearlyEqual(TEXT("持续期间内维持 0.2"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.2f, 0.0001f);

    TimeSubsystem->AdvanceTime(0.5f);
    TestTrue(TEXT("持续时间结束进入淡出阶段"), TimeSubsystem->IsChannelActive(ETimeDilationChannel::SlowMo));

    TimeSubsystem->AdvanceTime(0.5f);
    TestNearlyEqual(TEXT("淡出过半时插值接近 0.6"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.6f, 0.05f);

    TimeSubsystem->AdvanceTime(0.6f);
    TestFalse(TEXT("淡出完成通道关闭"), TimeSubsystem->IsChannelActive(ETimeDilationChannel::SlowMo));
    TestNearlyEqual(TEXT("完全还原为 1.0"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 1.0f, 0.0001f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameTimePersistentBlendOutTest, "Game.Core.Time.PersistentBlendOut",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 持久通道（无固定时长）也能按 BlendOut 平滑淡出并关闭
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FGameTimePersistentBlendOutTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTimeDilationTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UTimeDilationSubsystem* TimeSubsystem = World->GetSubsystem<UTimeDilationSubsystem>();
    TestNotNull(TEXT("时间膨胀子系统有效"), TimeSubsystem);
    if (!TimeSubsystem) {
        return false;
    }

    TimeSubsystem->ClearAllGlobalDilations();

    TimeSubsystem->RequestGlobalDilation(ETimeDilationChannel::SlowMo, 0.0f, 0.5f);
    TestNearlyEqual(TEXT("持久通道生效为 0.5"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 0.5f, 0.0001f);

    TimeSubsystem->ClearGlobalDilationWithBlend(ETimeDilationChannel::SlowMo, 0.2f);
    TestTrue(TEXT("淡出阶段通道保持激活"), TimeSubsystem->IsChannelActive(ETimeDilationChannel::SlowMo));

    TimeSubsystem->AdvanceTime(0.1f);
    TestTrue(TEXT("淡出未完成前仍激活"), TimeSubsystem->IsChannelActive(ETimeDilationChannel::SlowMo));
    TestTrue(TEXT("淡出中时间缩放在 0.5 与 1.0 之间"),
        TimeSubsystem->GetEffectiveGlobalTimeDilation() > 0.5f
        && TimeSubsystem->GetEffectiveGlobalTimeDilation() < 1.0f);

    TimeSubsystem->AdvanceTime(0.2f);
    TestFalse(TEXT("淡出完成后通道关闭"), TimeSubsystem->IsChannelActive(ETimeDilationChannel::SlowMo));
    TestNearlyEqual(TEXT("淡出完成后全局时间缩放恢复 1.0"), TimeSubsystem->GetEffectiveGlobalTimeDilation(), 1.0f, 0.0001f);
    return true;
}

#endif // WITH_AUTOMATION_TESTS

