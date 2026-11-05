/**
 * @file GameFeedbackHitStopLimiterTest.cpp
 * @brief 顿帧限制器冷却与累积预算自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Feedback/GameFeedbackActions.h"
#include "GameFramework/Actor.h"
#include "System/GameFeedbackHitStopLimiter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackHitStopLimiterTest,
    "Game.Feedback.HitStopLimiter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证攻击者与受击目标冷却窗口、累积时长预算限制与超时重置
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
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
    TestTrue(TEXT("首次命中应通过冷却"), Limiter.PassCooldown(Attacker, Target, Action, 0.0));
    TestFalse(TEXT("冷却窗口内再次命中应被拒绝"), Limiter.PassCooldown(Attacker, Target, Action, 0.02));
    TestTrue(TEXT("冷却结束后应重新放行"), Limiter.PassCooldown(Attacker, Target, Action, 0.10));

    TestTrue(TEXT("首次申请应获得完整时长"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.01), 0.08f));
    TestTrue(TEXT("第二次申请应获得完整时长"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.04), 0.08f));
    TestTrue(TEXT("第三次申请应被累积预算截断"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.08), 0.04f));
    TestTrue(TEXT("预算耗尽后应返回零"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.12), 0.0f));
    TestTrue(TEXT("越过重置窗口后累积预算应清零"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.90), 0.08f));

    Limiter.ClearActor(Target);
    TestTrue(TEXT("清除后应重新获得完整预算"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.95), 0.08f));
    TestEqual(TEXT("空 Actor 申请应返回 0"), Limiter.AcquireDuration(nullptr, 0.08f, Action, 1.0), 0.0f);
    TestEqual(TEXT("非正时长申请应返回 0"), Limiter.AcquireDuration(Target, 0.0f, Action, 1.0), 0.0f);
    Limiter.ClearActor(nullptr);
    Limiter.Prune(10.0);

    TestTrue(TEXT("无攻击者（环境伤害）首次命中应通过冷却"), Limiter.PassCooldown(nullptr, Target, Action, 10.0));
    Limiter.Prune(10.02);
    TestFalse(TEXT("无攻击者在冷却内再次命中经 Prune 后仍应被拒绝"), Limiter.PassCooldown(nullptr, Target, Action, 10.02));
    TestTrue(TEXT("无攻击者冷却结束后应重新放行"), Limiter.PassCooldown(nullptr, Target, Action, 10.10));

    AActor* StaleAttacker = NewObject<AActor>(GetTransientPackage());
    TestTrue(TEXT("临时攻击者首次命中应放行"), Limiter.PassCooldown(StaleAttacker, Target, Action, 10.15));
    StaleAttacker->MarkAsGarbage();
    Limiter.Prune(10.16);
    TestTrue(TEXT("攻击者被垃圾回收后相关条目应被 Prune 清理放行"), Limiter.PassCooldown(StaleAttacker, Target, Action, 10.16));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
