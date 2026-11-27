/**
 * @file GameFeedbackHitStopLimiterTest.cpp
 * @brief 顿帧限制器冷却、累积预算、长重置窗口与节流清理自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Feedback/GameFeedbackActions.h"
#include "GameFramework/Actor.h"
#include "Tracker/GameFeedbackHitStopLimiter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackHitStopLimiterTest,
    "Game.Feedback.HitStopLimiter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证攻击者与受击目标冷却窗口、累积时长预算限制、长 ResetWindow 保护与节流清理
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackHitStopLimiterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* Attacker = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    FGameFeedbackHitStopAction Action;
    Action.CooldownInterval = 0.05f;
    Action.MaxCumulativeDuration = 0.20f;
    Action.ResetWindow = 0.50f;
    Action.VictimDuration = 0.08f;

    FGameFeedbackHitStopLimiter Limiter;
    TestTrue(TEXT("双空实体冷却检查直接放行且不污染冷却表"), Limiter.PassCooldown(nullptr, nullptr, Action, 0.0));
    TestTrue(TEXT("首次命中应通过冷却"), Limiter.PassCooldown(Attacker, Target, Action, 0.0));
    TestFalse(TEXT("冷却窗口内再次命中应被拒绝"), Limiter.PassCooldown(Attacker, Target, Action, 0.02));
    TestTrue(TEXT("冷却结束后应重新放行"), Limiter.PassCooldown(Attacker, Target, Action, 0.10));

    TestNearlyEqual(TEXT("首次申请应获得完整时长"), Limiter.AcquireDuration(Target, 0.08f, Action, 0.01), 0.08f, 0.0001f);
    TestNearlyEqual(TEXT("第二次申请应获得完整时长"), Limiter.AcquireDuration(Target, 0.08f, Action, 0.04), 0.08f, 0.0001f);
    TestNearlyEqual(TEXT("第三次申请应被累积预算截断"), Limiter.AcquireDuration(Target, 0.08f, Action, 0.08), 0.04f, 0.0001f);
    TestNearlyEqual(TEXT("预算耗尽后应返回零"), Limiter.AcquireDuration(Target, 0.08f, Action, 0.12), 0.0f, 0.0001f);
    TestNearlyEqual(TEXT("越过重置窗口后累积预算应清零"), Limiter.AcquireDuration(Target, 0.08f, Action, 0.90), 0.08f, 0.0001f);

    Limiter.ClearActor(Target);
    TestNearlyEqual(TEXT("清除后应重新获得完整预算"), Limiter.AcquireDuration(Target, 0.08f, Action, 0.95), 0.08f, 0.0001f);
    TestEqual(TEXT("空 Actor 申请应返回 0"), Limiter.AcquireDuration(nullptr, 0.08f, Action, 1.0), 0.0f);
    TestEqual(TEXT("非正时长申请应返回 0"), Limiter.AcquireDuration(Target, 0.0f, Action, 1.0), 0.0f);
    Limiter.ClearActor(nullptr);
    Limiter.Prune(10.0);

    FGameFeedbackHitStopAction LongWindowAction = Action;
    LongWindowAction.ResetWindow = 8.0f;
    LongWindowAction.MaxCumulativeDuration = 0.10f;
    TestNearlyEqual(TEXT("长 ResetWindow 首次耗尽预算"), Limiter.AcquireDuration(Target, 0.10f, LongWindowAction, 100.0), 0.10f, 0.0001f);
    Limiter.Prune(106.0);
    TestNearlyEqual(TEXT("超过 5s 但未超过 8s ResetWindow 时 Prune 不得提前清零预算"), Limiter.AcquireDuration(Target, 0.05f, LongWindowAction, 106.0), 0.0f, 0.0001f);
    Limiter.Reset();
    TestNearlyEqual(TEXT("Reset 后预算立即恢复"), Limiter.AcquireDuration(Target, 0.05f, LongWindowAction, 106.5), 0.05f, 0.0001f);

    TestTrue(TEXT("无攻击者（环境伤害）首次命中应通过冷却"), Limiter.PassCooldown(nullptr, Target, Action, 110.0));
    Limiter.PruneIfNeeded(110.02);
    TestFalse(TEXT("无攻击者在冷却内再次命中经 PruneIfNeeded 后仍应被拒绝"), Limiter.PassCooldown(nullptr, Target, Action, 110.02));
    TestTrue(TEXT("无攻击者冷却结束后应重新放行"), Limiter.PassCooldown(nullptr, Target, Action, 110.10));

    TestTrue(TEXT("无目标仅有攻击者首次命中应通过冷却"), Limiter.PassCooldown(Attacker, nullptr, Action, 115.0));
    Limiter.Prune(115.01);
    TestFalse(TEXT("无目标仅有攻击者的活跃冷却记录不得被 Prune 提前误删"), Limiter.PassCooldown(Attacker, nullptr, Action, 115.02));
    TestTrue(TEXT("无目标仅有攻击者冷却结束后应重新放行"), Limiter.PassCooldown(Attacker, nullptr, Action, 115.10));

    AActor* StaleAttacker = NewObject<AActor>(GetTransientPackage());
    TestTrue(TEXT("临时攻击者首次命中应放行"), Limiter.PassCooldown(StaleAttacker, Target, Action, 110.15));
    StaleAttacker->MarkAsGarbage();
    Limiter.Prune(110.16);
    TestTrue(TEXT("攻击者被垃圾回收后相关条目应被 Prune 清理放行"), Limiter.PassCooldown(StaleAttacker, Target, Action, 110.16));

    TArray<AActor*> DeadAttackers;
    DeadAttackers.Reserve(32);
    for (int32 Index = 0; Index < 32; ++Index) {
        AActor* DeadAttacker = NewObject<AActor>(GetTransientPackage());
        TestTrue(TEXT("批量攻击者首次冷却放行"), Limiter.PassCooldown(DeadAttacker, Target, Action, 120.0));
        DeadAttackers.Add(DeadAttacker);
    }
    for (AActor* DeadAttacker : DeadAttackers) {
        DeadAttacker->MarkAsGarbage();
    }
    for (int32 Index = 0; Index < 16; ++Index) {
        Limiter.ClearActor(DeadAttackers[Index]);
    }
    for (int32 Index = 16; Index < 32; ++Index) {
        TestFalse(TEXT("批量销毁哈希碰撞下未 Clear 的失效攻击者冷却键不受误删"), Limiter.PassCooldown(DeadAttackers[Index], Target, Action, 120.01));
    }
    Limiter.Prune(120.02);
    for (AActor* DeadAttacker : DeadAttackers) {
        TestTrue(TEXT("Prune 后全部失效攻击者冷却记录已清理"), Limiter.PassCooldown(DeadAttacker, Target, Action, 120.02));
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
