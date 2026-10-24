/**
 * @file FPerformanceTest.cpp
 * @brief GameCore 性能基准自动化测试（缓存查找 / 事件总线吞吐），Shipping 构建自动剔除
 *
 * 预算刻意放宽，只拦截数量级退化，不把正常机器抖动当作失败。
 */

#include "Cache/FVersionedCache.h"
#include "Listener/FListenerHub.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace {

constexpr int32 PerfCacheKeyCount = 4096;
constexpr int32 PerfCacheIterations = 200000;
constexpr double PerfCacheBudgetNsPerOp = 2000.0;

constexpr int32 PerfListenerIterations = 100000;
constexpr double PerfListenerBudgetNsPerOp = 5000.0;

struct FPerfCacheValue {
    int32 Payload = 0;
};

double MeasureNsPerOp(const int32 Iterations, const TFunction<void()>& Body) {
    const double StartSeconds = FPlatformTime::Seconds();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        Body();
    }
    const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
    return (ElapsedSeconds * 1.0e9) / static_cast<double>(Iterations);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameCacheFindPerfTest, "Game.Core.Perf.VersionedCacheFind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief Eager 缓存查找吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且结果正确返回 true
 *
 * 实现思路：灌入 4096 键并对全部键预热，循环查找并校验命中，按总耗时折算单次纳秒；
 * 预算 2000ns/op 仅拦截数量级退化（正常桌面环境为几十纳秒）。
 */
bool FGameCacheFindPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FVersionedCache<FPerfCacheValue> Cache(FName(TEXT("PerfCache")));

    TArray<FName> Keys;
    Keys.Reserve(PerfCacheKeyCount);
    for (int32 Index = 0; Index < PerfCacheKeyCount; ++Index) {
        Keys.Add(FName(*FString::Printf(TEXT("Perf.Cache.Key.%d"), Index)));
    }

    Cache.Rebuild(Keys, [](FName Key) {
        FPerfCacheValue Value;
        Value.Payload = static_cast<int32>(Key.ToString().Len());
        return Value;
    });

    int32 HitCount = 0;
    const double NsPerOp = MeasureNsPerOp(PerfCacheIterations, [&Cache, &Keys, &HitCount]() {
        const FName& Key = Keys[HitCount % PerfCacheKeyCount];
        const TSharedPtr<const FPerfCacheValue> Value = Cache.FindShared(Key);
        if (Value.IsValid()) {
            ++HitCount;
        }
    });

    TestEqual(TEXT("全部查找均应命中"), HitCount, PerfCacheIterations);
    TestTrue(
        FString::Printf(TEXT("Eager 缓存查找 %.1f ns/op 应低于预算 %.0f ns/op"), NsPerOp, PerfCacheBudgetNsPerOp),
        NsPerOp < PerfCacheBudgetNsPerOp);

    AddInfo(FString::Printf(TEXT("PerfCacheFind: %.1f ns/op (%d ops)"), NsPerOp, PerfCacheIterations));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameListenerHubPerfTest, "Game.Core.Perf.ListenerHubThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 事件总线单订阅广播吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且回调次数正确返回 true
 *
 * 实现思路：订阅整型载荷事件后连续广播，统计回调次数与单次纳秒；
 * 预算 5000ns/op 覆盖 COW 快照与回调执行，仅拦截数量级退化。
 */
bool FGameListenerHubPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    static const FName PerfEventName(TEXT("Perf.ListenerHub.Event"));

    int32 ReceivedCount = 0;
    const TSharedRef<FObserverSubscription> Subscription = FListenerHub::Get().Subscribe<int32>(
        PerfEventName,
        [&ReceivedCount](const int32& Payload) {
            ReceivedCount += Payload;
        });

    const double NsPerOp = MeasureNsPerOp(PerfListenerIterations, [&ReceivedCount]() {
        FListenerHub::Get().Emit<int32>(PerfEventName, 1);
    });

    TestEqual(TEXT("回调次数应与广播次数一致"), ReceivedCount, PerfListenerIterations);
    TestTrue(
        FString::Printf(TEXT("事件总线广播 %.1f ns/op 应低于预算 %.0f ns/op"), NsPerOp, PerfListenerBudgetNsPerOp),
        NsPerOp < PerfListenerBudgetNsPerOp);

    AddInfo(FString::Printf(TEXT("ListenerHubThroughput: %.1f ns/op (%d ops)"), NsPerOp, PerfListenerIterations));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
