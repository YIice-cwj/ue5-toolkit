/**
 * @file NetReconnectPolicyTest.cpp
 * @brief 指数退避带双向抖动自动重连策略（FNetReconnectPolicy）深度单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Policy/NetReconnectPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetReconnectPolicyTest,
    "Game.Net.Service.ReconnectPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证指数倍增上限钳制、随机抖动包络区间、倒计时推进与最大次数耗尽熔断
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetReconnectPolicyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetReconnectConfig ZeroJitterCfg;
    ZeroJitterCfg.bEnableAutoReconnect = true;
    ZeroJitterCfg.BaseDelaySeconds = 1.0f;
    ZeroJitterCfg.MaxDelaySeconds = 5.0f;
    ZeroJitterCfg.MaxRetryAttempts = 4;
    ZeroJitterCfg.JitterRatio = 0.0f;

    FNetReconnectPolicy Policy(ZeroJitterCfg, 42);
    TestEqual(TEXT("初始尝试次数为 0"), Policy.GetCurrentAttempt(), 0);
    TestFalse(TEXT("初始未处于等待状态"), Policy.IsWaitingForRetry());
    TestFalse(TEXT("初始未耗尽重试配额"), Policy.IsExhausted());
    TestFalse(TEXT("未调度时推进计时器返回 false"), Policy.AdvanceTimer(1.0f));

    const TOptional<float> Delay1 = Policy.ScheduleNextAttempt();
    TestTrue(TEXT("第 1 次重连调度成功且退避为 1.0s"), Delay1.IsSet() && FMath::IsNearlyEqual(*Delay1, 1.0f));
    TestEqual(TEXT("当前尝试次数为 1"), Policy.GetCurrentAttempt(), 1);
    TestTrue(TEXT("调度后处于等待重连状态"), Policy.IsWaitingForRetry());
    TestFalse(TEXT("非正时间步进不触发重连"), Policy.AdvanceTimer(0.0f));
    TestFalse(TEXT("推进 0.6s 尚未到期"), Policy.AdvanceTimer(0.6f));
    TestTrue(TEXT("剩余秒数约为 0.4s"), FMath::IsNearlyEqual(Policy.GetRemainingDelaySeconds(), 0.4f, 1e-3f));
    TestTrue(TEXT("再推进 0.5s 跨过零点触发重连"), Policy.AdvanceTimer(0.5f));
    TestFalse(TEXT("触发后等待状态清零"), Policy.IsWaitingForRetry());

    const TOptional<float> Delay2 = Policy.ScheduleNextAttempt();
    const TOptional<float> Delay3 = Policy.ScheduleNextAttempt();
    const TOptional<float> Delay4 = Policy.ScheduleNextAttempt();
    TestTrue(TEXT("第 2 次指数退避为 2.0s"), Delay2.IsSet() && FMath::IsNearlyEqual(*Delay2, 2.0f));
    TestTrue(TEXT("第 3 次指数退避为 4.0s"), Delay3.IsSet() && FMath::IsNearlyEqual(*Delay3, 4.0f));
    TestTrue(TEXT("第 4 次被 MaxDelaySeconds 钳制为 5.0s"), Delay4.IsSet() && FMath::IsNearlyEqual(*Delay4, 5.0f));

    const TOptional<float> ExhaustedDelay = Policy.ScheduleNextAttempt();
    TestFalse(TEXT("超出 MaxRetryAttempts(4) 后返回空值触发熔断"), ExhaustedDelay.IsSet());
    TestTrue(TEXT("耗尽标志位为 true"), Policy.IsExhausted());
    TestFalse(TEXT("耗尽后不再处于等待状态"), Policy.IsWaitingForRetry());

    Policy.Reset();
    TestEqual(TEXT("Reset 后尝试次数归零"), Policy.GetCurrentAttempt(), 0);
    TestFalse(TEXT("Reset 后耗尽标志位清除"), Policy.IsExhausted());

    FNetReconnectConfig JitterCfg = ZeroJitterCfg;
    JitterCfg.MaxDelaySeconds = 20.0f;
    JitterCfg.MaxRetryAttempts = 10;
    JitterCfg.JitterRatio = 0.25f;
    Policy.SetConfig(JitterCfg);
    TestTrue(TEXT("SetConfig 同步生效"), FMath::IsNearlyEqual(Policy.GetConfig().JitterRatio, 0.25f));

    const TOptional<float> JitterSample = Policy.ScheduleNextAttempt();
    TestTrue(
        TEXT("25% 抖动下首次 1.0s 退避严格落在 [0.75s, 1.25s] 区间内"),
        JitterSample.IsSet() && *JitterSample >= 0.75f && *JitterSample <= 1.25f
    );

    FNetReconnectConfig DisabledCfg = ZeroJitterCfg;
    DisabledCfg.bEnableAutoReconnect = false;
    Policy.SetConfig(DisabledCfg);
    TestFalse(TEXT("关闭自动重连开关时直接返回空值"), Policy.ScheduleNextAttempt().IsSet());
    TestTrue(TEXT("关闭自动重连时标记为已耗尽"), Policy.IsExhausted());

    return true;
}

#endif

