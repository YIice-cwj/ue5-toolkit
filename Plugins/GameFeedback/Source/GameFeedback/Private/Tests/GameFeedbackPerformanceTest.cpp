/**
 * @file GameFeedbackPerformanceTest.cpp
 * @brief GameFeedback 核心模块微基准性能测试（预测历史 / 顿帧限流 / 资产注册表检索 / 全局膨胀混音器）
 *
 * 遵循通用代码规范与 UE5 性能基准规范：多轮采样消除预热偏差，统计中位数与变异系数 (CV%)。
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Feedback/GameFeedbackActions.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "System/GameFeedbackHitStopLimiter.h"
#include "System/GameFeedbackPredictionHistory.h"
#include "Time/FGlobalDilationMixer.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 FastContainerIterations = 10000;
constexpr int32 RegistryIterations = 5000;

constexpr double PredictionHistoryBudgetNsPerOp = 2000.0;
constexpr double HitStopLimiterBudgetNsPerOp = 3000.0;
constexpr double RegistryLookupBudgetNsPerOp = 5000.0;
constexpr double DilationMixerBudgetNsPerOp = 1000.0;

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮微基准测试，消除预热偏差并统计中位数与变异系数
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热轮数（测量后丢弃）
 * @param SampleRounds 正式采样轮数
 * @param IterationsPerRound 每轮迭代执行次数
 * @param Body 待执行测量闭包
 * @return 包含中位数与离散度的统计度量
 */
template <typename TFunc>
FBenchmarkMetrics RunBenchmark(
    const int32 WarmupRounds,
    const int32 SampleRounds,
    const int32 IterationsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(IterationsPerRound);
    }

    TArray<double> SamplesNs;
    SamplesNs.Reserve(SampleRounds);

    double SumNs = 0.0;
    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const double StartTime = FPlatformTime::Seconds();
        Body(IterationsPerRound);
        const double ElapsedSec = FPlatformTime::Seconds() - StartTime;
        const double NsPerOp = (ElapsedSec * 1.0e9) / static_cast<double>(IterationsPerRound);
        SamplesNs.Add(NsPerOp);
        SumNs += NsPerOp;
    }

    SamplesNs.Sort();
    const double MedianNs = SamplesNs[SampleRounds / 2];
    const double MeanNs = SumNs / static_cast<double>(SampleRounds);

    double VarianceSum = 0.0;
    for (const double Sample : SamplesNs) {
        const double Diff = Sample - MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = FMath::Sqrt(VarianceSum / static_cast<double>(SampleRounds));
    const double CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;

    FBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = CvPercent;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackPredictionHistoryPerfTest,
    "Game.Feedback.Perf.PredictionHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 本地预测历史队列登记与消费匹配微基准测试
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且统计正常返回 true
 */
bool FGameFeedbackPredictionHistoryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* Attacker = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    volatile int32 MatchCount = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FastContainerIterations,
        [Attacker, Target, &MatchCount](const int32 Iterations) {
            FGameFeedbackPredictionHistory History;
            const double BaseTime = 100.0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const uint16 EventId = static_cast<uint16>((Index % 500) + 1);
                History.Add(EventId, Attacker, Target, BaseTime + Index * 0.001);
                const bool bMatched = History.ConsumeMatch(EventId, Attacker, Target, BaseTime + Index * 0.001 + 0.0001);
                if (bMatched) {
                    MatchCount = MatchCount + 1;
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("PredictionHistory 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PredictionHistoryBudgetNsPerOp),
        Metrics.MedianNs < PredictionHistoryBudgetNsPerOp
    );
    TestTrue(TEXT("预测匹配应成功执行"), MatchCount > 0);

    AddInfo(FString::Printf(
        TEXT("PredictionHistory: 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackHitStopLimiterPerfTest,
    "Game.Feedback.Perf.HitStopLimiter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 顿帧限流器冷却判断与累积时长申请微基准测试
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且统计正常返回 true
 */
bool FGameFeedbackHitStopLimiterPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* Attacker = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    FGameFeedbackHitStopAction Action;
    Action.CooldownInterval = 0.01f;
    Action.MaxCumulativeDuration = 1.0f;
    Action.ResetWindow = 0.5f;
    Action.VictimDuration = 0.05f;

    volatile float TotalDuration = 0.0f;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FastContainerIterations,
        [Attacker, Target, &Action, &TotalDuration](const int32 Iterations) {
            FGameFeedbackHitStopLimiter Limiter;
            double CurrentTime = 0.0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                CurrentTime += 0.02;
                if (Limiter.PassCooldown(Attacker, Target, Action, CurrentTime)) {
                    TotalDuration = TotalDuration + Limiter.AcquireDuration(Target, Action.VictimDuration, Action, CurrentTime);
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("HitStopLimiter 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, HitStopLimiterBudgetNsPerOp),
        Metrics.MedianNs < HitStopLimiterBudgetNsPerOp
    );
    TestTrue(TEXT("顿帧时长累加应生效"), TotalDuration > 0.0f);

    AddInfo(FString::Printf(
        TEXT("HitStopLimiter: 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackRegistryLookupPerfTest,
    "Game.Feedback.Perf.RegistryLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 反馈注册表双层索引与标签决策微基准测试
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且决策正常返回 true
 */
bool FGameFeedbackRegistryLookupPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* DefaultDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* SlashDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* FireDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* DeepDef = NewObject<UGameFeedbackDefinition>();

    Registry->DefaultFallbackDefinition = DefaultDef;

    const FGameplayTag SlashChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Perf.Slash")));
    const FGameplayTag FireChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Perf.Fire")));
    const FGameplayTag BluntChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Perf.Blunt")));

    Registry->ExactChannelMap.Add(SlashChannel, SlashDef);
    Registry->ExactChannelMap.Add(FireChannel, FireDef);

    const FGameplayTag ShallowTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Perf.Shallow")));
    const FGameplayTag DeepTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Perf.A.B.C.Deep")));

    Registry->ExactFeedbackTagMap.Add(DeepTag, DeepDef);
    Registry->RebuildDefinitionIndex();

    FGameplayTagContainer TestTags;
    TestTags.AddTag(ShallowTag);
    TestTags.AddTag(DeepTag);

    volatile int32 ValidHits = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RegistryIterations,
        [Registry, SlashChannel, FireChannel, BluntChannel, &TestTags, &ValidHits](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const FGameplayTag& Channel = (Index % 3 == 0) ? SlashChannel : ((Index % 3 == 1) ? FireChannel : BluntChannel);
                const TSoftObjectPtr<UGameFeedbackDefinition> Resolved = Registry->ResolveDefinitionPtr(Channel, TestTags);
                if (Resolved.IsValid() || !Resolved.IsNull()) {
                    ValidHits = ValidHits + 1;
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("RegistryLookup 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, RegistryLookupBudgetNsPerOp),
        Metrics.MedianNs < RegistryLookupBudgetNsPerOp
    );
    TestTrue(TEXT("注册表检索应全部命中有效资产指针"), ValidHits > 0);

    AddInfo(FString::Printf(
        TEXT("RegistryLookup: 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDilationMixerPerfTest,
    "Game.Feedback.Perf.DilationMixer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 全局分层时间膨胀混音器步进与重算微基准测试
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且生效值计算正确返回 true
 */
bool FGameFeedbackDilationMixerPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    volatile float SumDilation = 0.0f;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FastContainerIterations,
        [&SumDilation](const int32 Iterations) {
            FGlobalDilationMixer Mixer;
            Mixer.Request(ETimeDilationChannel::SlowMo, 1.0f, 0.5f, 0.2f);
            Mixer.Request(ETimeDilationChannel::HitStop, 0.05f, 0.05f, 0.02f);

            for (int32 Index = 0; Index < Iterations; ++Index) {
                Mixer.Advance(0.016f);
                if (Index % 10 == 0) {
                    Mixer.Request(ETimeDilationChannel::HitStop, 0.05f, 0.05f, 0.02f);
                }
                SumDilation = SumDilation + Mixer.GetEffectiveDilation();
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("DilationMixer 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, DilationMixerBudgetNsPerOp),
        Metrics.MedianNs < DilationMixerBudgetNsPerOp
    );
    TestTrue(TEXT("时间混音器有效缩放累加值应大于零"), SumDilation > 0.0f);

    AddInfo(FString::Printf(
        TEXT("DilationMixer: 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
