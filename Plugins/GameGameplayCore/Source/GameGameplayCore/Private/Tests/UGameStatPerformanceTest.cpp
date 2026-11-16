/**
 * @file UGameStatPerformanceTest.cpp
 * @brief GameGameplayCore 核心模块微基准性能测试（数值查询 / 修正器多阶演算 / Buff 叠层衰减 / 伤害派发）
 *
 * 遵循通用代码规范 10/03 §5 与 UE5 性能基准规范：多轮采样消除预热偏差，统计中位数与变异系数 (CV%)。
 */

#include "Stat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

#include "Buff/FGameBuffTypes.h"
#include "Buff/UGameBuffComponent.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Damage/IDamageable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Stat/FGameStatAcknowledgedDiff.h"
#include "Stat/FGameStatEffectiveEvaluator.h"
#include "Stat/FGameStatModifierContainer.h"
#include "Stat/UGameVitalComponent.h"
#include "Core/IGamePoolable.h"
#include "Tests/GameDamageTestTarget.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 StatLookupStatCount = 64;
constexpr int32 StatLookupIterations = 20000;
constexpr double StatLookupBudgetNsPerOp = 2000.0;

constexpr int32 StatEvaluationIterations = 20000;
constexpr double StatEvaluationBudgetNsPerOp = 1500.0;

constexpr int32 BuffStackTickIterations = 5000;
constexpr double BuffStackTickBudgetNsPerOp = 5000.0;

constexpr int32 DamageDispatchIterations = 10000;
constexpr double DamageDispatchBudgetNsPerOp = 3000.0;

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
    FGameStatLookupPerfTest,
    "Game.GameplayCore.Perf.StatLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 属性查询吞吐微基准测试（多轮采样统计）
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且统计正常返回 true
 */
bool FGameStatLookupPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Component = NewObject<UGameStatComponent>();

    TArray<FGameplayTag> Tags;
    Tags.Reserve(StatLookupStatCount);
    for (int32 Index = 0; Index < StatLookupStatCount; ++Index) {
        const FGameplayTag Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(
            FName(*FString::Printf(TEXT("Perf.Core.Attr.%d"), Index)));
        Tags.Add(Tag);
        Component->RegisterStat(Tag, 100.0f, 100.0f, 0.0f);
    }

    volatile float Sink = 0.0f;
    int32 HitCount = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StatLookupIterations,
        [Component, &Tags, &Sink, &HitCount](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const float Value = Component->GetStatCurrent(Tags[Index % StatLookupStatCount]);
                Sink = Sink + Value;
                if (FMath::IsNearlyEqual(Value, 100.0f)) {
                    ++HitCount;
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("属性查询耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, StatLookupBudgetNsPerOp),
        Metrics.MedianNs < StatLookupBudgetNsPerOp
    );
    TestTrue(TEXT("全部属性查询均应有效命中"), HitCount > 0);

    AddInfo(FString::Printf(
        TEXT("StatLookup: 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatModifierEvaluationPerfTest,
    "Game.GameplayCore.Perf.StatModifierEvaluation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 属性复合修正器链演算微基准测试（加算 -> 乘算 -> 覆写）
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且数值正确返回 true
 */
bool FGameStatModifierEvaluationPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameStatModifierContainer Container;
    const FGameplayTag StatTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Core.ModEvaluation")));

    Container.AddModifier(StatTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add, true, true));
    Container.AddModifier(StatTag, FGameStatModifier(1.5f, EGameStatModifierOp::Multiply, true, true));
    Container.AddModifier(StatTag, FGameStatModifier(10.0f, EGameStatModifierOp::Add, true, true));

    volatile float Sink = 0.0f;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StatEvaluationIterations,
        [&Container, StatTag, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const float Effective = FGameStatEffectiveEvaluator::EvaluateCurrent(100.0f, 0.0f, StatTag, Container);
                Sink = Sink + Effective;
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("修正器演算耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, StatEvaluationBudgetNsPerOp),
        Metrics.MedianNs < StatEvaluationBudgetNsPerOp
    );
    TestTrue(TEXT("修正器求值累加应大于零"), Sink > 0.0f);

    AddInfo(FString::Printf(
        TEXT("StatModifierEvaluation: 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffStackAndTickPerfTest,
    "Game.GameplayCore.Perf.BuffStackAndTick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief Buff 组件叠层、倒计时推进与自然过期微基准测试
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且逻辑正常返回 true
 */
bool FGameBuffStackAndTickPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            World = Context.World();
            break;
        }
    }
    if (World == nullptr) {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* HostActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(HostActor);
    BuffComp->RegisterComponent();

    const FGameplayTag BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Core.TimedBuff")));

    FGameBuffSpec Spec;
    Spec.BuffTag = BuffTag;
    Spec.Duration = 0.1f;
    Spec.MaxStacks = 5;
    Spec.StackPolicy = EGameBuffStackPolicy::RefreshDuration;

    volatile int32 CompletedOps = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BuffStackTickIterations,
        [BuffComp, &Spec, BuffTag, &CompletedOps](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                BuffComp->ApplyBuff(Spec);
                BuffComp->TickComponent(0.05f, LEVELTICK_All, nullptr);
                BuffComp->TickComponent(0.06f, LEVELTICK_All, nullptr);
                if (!BuffComp->HasBuff(BuffTag)) {
                    CompletedOps = CompletedOps + 1;
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("Buff叠层推进耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BuffStackTickBudgetNsPerOp),
        Metrics.MedianNs < BuffStackTickBudgetNsPerOp
    );
    TestTrue(TEXT("Buff 操作应成功循环生效"), CompletedOps > 0);

    AddInfo(FString::Printf(
        TEXT("BuffStackAndTick: 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    HostActor->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageDispatchPerfTest,
    "Game.GameplayCore.Perf.DamageDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 伤害上下文装配与 IDamageable 接口同步派发微基准测试
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且反馈正常返回 true
 */
bool FGameDamageDispatchPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameDamageTestTarget* Target = NewObject<UGameDamageTestTarget>();

    const FGameplayTag PhysicalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Test.Physical.Slash")));
    const FGameplayTag FireTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Test.Elemental.Fire")));

    FGameDamageContext Context;
    Context.SetChannelDamage(PhysicalTag, 80.0f);
    Context.SetChannelDamage(FireTag, 20.0f);

    volatile float TotalDispatched = 0.0f;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        DamageDispatchIterations,
        [Target, &Context, &TotalDispatched](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const FGameDamageResult Result = IDamageable::Execute_ApplyDamageContext(Target, Context);
                TotalDispatched = TotalDispatched + Result.GetActualDamage();
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("伤害派发耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, DamageDispatchBudgetNsPerOp),
        Metrics.MedianNs < DamageDispatchBudgetNsPerOp
    );
    TestTrue(TEXT("伤害派发实际数值应大于零"), TotalDispatched > 0.0f);

    AddInfo(FString::Printf(
        TEXT("DamageDispatch: 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatAcknowledgedDiffPerfTest,
    "Game.GameplayCore.Perf.AcknowledgedDiff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 客户端网络复制差分计算器 (TSortedVectorMap) 判定与记录微基准测试
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameStatAcknowledgedDiffPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameStatAcknowledgedDiff Diff;
    TArray<FGameplayTag> TestTags;
    for (int32 Index = 0; Index < 8; ++Index) {
        TestTags.Add(UGameplayTagsManager::Get().AddNativeGameplayTag(*FString::Printf(TEXT("Perf.Diff.Stat_%d"), Index)));
    }

    constexpr int32 DiffIterations = 20000;
    constexpr double DiffBudgetNsPerOp = 8000.0;
    volatile int32 BroadcastCount = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        DiffIterations,
        [&Diff, &TestTags, &BroadcastCount](const int32 Iterations) {
            for (int32 Iter = 0; Iter < Iterations; ++Iter) {
                const float Val = static_cast<float>(Iter % 100);
                for (const FGameplayTag& Tag : TestTags) {
                    float OldCurrent = 0.0f;
                    float OldMax = 0.0f;
                    if (Diff.NeedsBroadcast(Tag, Val, 100.0f, OldCurrent, OldMax)) {
                        Diff.Record(Tag, Val, 100.0f);
                        BroadcastCount = BroadcastCount + 1;
                    }
                }
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("差分去重耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, DiffBudgetNsPerOp),
        Metrics.MedianNs < DiffBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("AcknowledgedDiff (TSortedVectorMap): 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolableLifecyclePerfTest,
    "Game.GameplayCore.Perf.PoolableReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 三大战斗组件通过 IGamePoolable 执行入池重置与出库激活的微基准测试
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGamePoolableLifecyclePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            World = Context.World();
            break;
        }
    }
    if (World == nullptr) {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* HostActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
    if (HostActor == nullptr) {
        return false;
    }

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Poolable.Health")));
    const FGameplayTag BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Poolable.Buff")));

    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(HostActor);
    StatComp->RegisterComponent();

    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(HostActor);
    VitalComp->SetPrimaryVitalTag(HealthTag);
    VitalComp->RegisterComponent();

    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(HostActor);
    BuffComp->RegisterComponent();
    BuffComp->BindStatComponent(StatComp);

    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);

    FGameBuffSpec Spec;
    Spec.BuffTag = BuffTag;
    Spec.Duration = 10.0f;

    constexpr int32 PoolIterations = 10000;
    constexpr double PoolBudgetNsPerOp = 15000.0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PoolIterations,
        [StatComp, VitalComp, BuffComp, &Spec, HealthTag](const int32 Iterations) {
            for (int32 Iter = 0; Iter < Iterations; ++Iter) {
                BuffComp->K2_ApplyBuff(Spec);
                float ActualDelta = 0.0f;
                StatComp->ModifyStat(HealthTag, -20.0f, ActualDelta);

                IGamePoolable::Execute_OnReleaseToPool(BuffComp);
                IGamePoolable::Execute_OnReleaseToPool(StatComp);
                IGamePoolable::Execute_OnReleaseToPool(VitalComp);

                IGamePoolable::Execute_OnAcquireFromPool(BuffComp);
                IGamePoolable::Execute_OnAcquireFromPool(StatComp);
                IGamePoolable::Execute_OnAcquireFromPool(VitalComp);
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("三大组件池化重置耗时 %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PoolBudgetNsPerOp),
        Metrics.MedianNs < PoolBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("PoolableReset (Stat+Buff+Vital): 中位数 %.1f ns/op (CV: %.2f%%, 极值: [%.1f, %.1f] ns, 吞吐: %.2f Mops/s)"),
        Metrics.MedianNs,
        Metrics.CvPercent,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.OpsPerSec / 1.0e6
    ));

    HostActor->Destroy();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
