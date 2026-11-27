/**
 * @file GameGameplayCorePerformanceTest.cpp
 * @brief GameGameplayCore 全组件热路径 12 项微基准性能测试套件（2 轮预热 + 7 轮采样，输出 Median/Mean/CV%/Ops）
 *
 * 遵循通用代码规范 10/03 §5 与 UE5 性能基准规范：多轮采样消除预热偏差，同步输出 Display 日志与 Automation Info。
 */

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Buff/FGameBuffInstance.h"
#include "Buff/FGameBuffTypes.h"
#include "Buff/UGameBuffComponent.h"
#include "Core/IGamePoolable.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Damage/IDamageable.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "Logging/GameLogCategories.h"
#include "Misc/AutomationTest.h"
#include "Stat/FGameStatAcknowledgedDiff.h"
#include "Stat/FGameStatModifierContainer.h"
#include "Stat/UGameStatComponent.h"
#include "Stat/UGameVitalComponent.h"
#include "Tests/GameGameplayCoreTestUtils.h"
#include "Tests/UGameDamageTestTarget.h"

using Game::GameplayCore::Test::FScopedTestHostActor;

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 7;

constexpr int32 StatLookupStatCount = 64;
constexpr int32 StatLookupIterations = 20000;
constexpr double StatLookupBudgetNsPerOp = 2000.0;

constexpr int32 StatEvaluationIterations = 20000;
constexpr double StatEvaluationBudgetNsPerOp = 1500.0;

constexpr int32 StatEffectivePercentIterations = 20000;
constexpr double StatEffectivePercentBudgetNsPerOp = 1500.0;

constexpr int32 StatModifyBroadcastIterations = 10000;
constexpr double StatModifyBroadcastBudgetNsPerOp = 3000.0;

constexpr int32 AcknowledgedDiffIterations = 20000;
constexpr double AcknowledgedDiffBudgetNsPerOp = 4000.0;

constexpr int32 VitalTransitionIterations = 10000;
constexpr double VitalTransitionBudgetNsPerOp = 3000.0;

constexpr int32 BuffStackTickIterations = 5000;
constexpr double BuffStackTickBudgetNsPerOp = 5000.0;

constexpr int32 BuffRefreshWithStatIterations = 10000;
constexpr double BuffRefreshWithStatBudgetNsPerOp = 3500.0;

constexpr int32 BuffGrantedTagQueryIterations = 20000;
constexpr double BuffGrantedTagQueryBudgetNsPerOp = 1500.0;

constexpr int32 BuffReplicatedDiffIterations = 10000;
constexpr double BuffReplicatedDiffBudgetNsPerOp = 4000.0;

constexpr int32 DamageDispatchIterations = 10000;
constexpr double DamageDispatchBudgetNsPerOp = 4500.0;

constexpr int32 PoolableResetIterations = 10000;
constexpr double PoolableResetBudgetNsPerOp = 8000.0;

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮预热与正式采样微基准，计算中位数、均值、极值、离散度与吞吐量
 * @tparam TFunc 待测循环体闭包类型
 * @param WarmupRounds 预热轮数（丢弃）
 * @param SampleRounds 正式采样轮数
 * @param IterationsPerRound 每轮迭代次数
 * @param Body 待测闭包
 * @return 统计指标结构体
 */
template <typename TFunc>
FBenchmarkMetrics RunBenchmark(
    int32 WarmupRounds,
    int32 SampleRounds,
    int32 IterationsPerRound,
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

/**
 * @brief 将微基准统计报告同时写入 LogGameGameplayCore Display 日志与 Automation Info
 * @param Test 当前自动化测试实例引用
 * @param BenchName 基准测试名称
 * @param Metrics 实测统计指标
 */
void EmitBenchmarkReport(FAutomationTestBase& Test, const TCHAR* BenchName, const FBenchmarkMetrics& Metrics) {
    const FString Report = FString::Printf(
        TEXT("[GameGameplayCorePerf] %s: Median=%.1f ns/op (Mean=%.1f, Min=%.1f, Max=%.1f, CV=%.2f%%, %.2f Mops/s)"),
        BenchName,
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    Test.AddInfo(Report);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatLookupPerfTest,
    "Game.GameplayCore.Perf.StatLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 1：64 属性规模下基础值查询吞吐
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
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
        [Component, &Tags, &Sink, &HitCount](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const float Value = Component->GetStatCurrent(Tags[Index % StatLookupStatCount]);
                Sink = Sink + Value;
                if (FMath::IsNearlyEqual(Value, 100.0f)) {
                    ++HitCount;
                }
            }
        }
    );

    TestTrue(FString::Printf(TEXT("StatLookup %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, StatLookupBudgetNsPerOp),
        Metrics.MedianNs < StatLookupBudgetNsPerOp);
    TestTrue(TEXT("全部属性查询均有效命中"), HitCount > 0);
    EmitBenchmarkReport(*this, TEXT("StatLookup"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatModifierEvaluationPerfTest,
    "Game.GameplayCore.Perf.StatModifierEvaluation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 2：属性复合修正器链（内联 4 槽零堆分配）演算吞吐
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameStatModifierEvaluationPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameStatModifierContainer Container;
    const FGameplayTag StatTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Core.ModEvaluation")));

    (void)Container.AddModifier(StatTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add, true, true));
    (void)Container.AddModifier(StatTag, FGameStatModifier(1.5f, EGameStatModifierOp::Multiply, true, true));
    (void)Container.AddModifier(StatTag, FGameStatModifier(10.0f, EGameStatModifierOp::Add, true, true));

    volatile float Sink = 0.0f;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StatEvaluationIterations,
        [&Container, StatTag, &Sink](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const float Effective = Container.EvaluateClamped(StatTag, 100.0f, 0.0f, false);
                Sink = Sink + Effective;
            }
        }
    );

    TestTrue(FString::Printf(TEXT("StatModifierEvaluation %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, StatEvaluationBudgetNsPerOp),
        Metrics.MedianNs < StatEvaluationBudgetNsPerOp);
    TestTrue(TEXT("修正器求值累加应大于零"), Sink > 0.0f);
    EmitBenchmarkReport(*this, TEXT("StatModifierEvaluation"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatEffectivePercentPerfTest,
    "Game.GameplayCore.Perf.StatEffectivePercent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 3：单次哈希查找 + 单趟双轴演算的 GetStatEffectivePercent 高频 UI 轮询热路径
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameStatEffectivePercentPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Component = NewObject<UGameStatComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Core.EffectivePercent")));
    Component->RegisterStat(HealthTag, 75.0f, 100.0f, 0.0f);
    Component->AddStatModifier(HealthTag, FGameStatModifier(25.0f, EGameStatModifierOp::Add, true, true));
    Component->AddStatModifier(HealthTag, FGameStatModifier(1.2f, EGameStatModifierOp::Multiply, false, true));

    volatile float Sink = 0.0f;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StatEffectivePercentIterations,
        [Component, HealthTag, &Sink](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Sink = Sink + Component->GetStatEffectivePercent(HealthTag);
            }
        }
    );

    TestTrue(FString::Printf(TEXT("StatEffectivePercent %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, StatEffectivePercentBudgetNsPerOp),
        Metrics.MedianNs < StatEffectivePercentBudgetNsPerOp);
    TestTrue(TEXT("派生百分比累加应大于零"), Sink > 0.0f);
    EmitBenchmarkReport(*this, TEXT("StatEffectivePercent"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatModifyAndBroadcastPerfTest,
    "Game.GameplayCore.Perf.StatModifyAndBroadcast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 4：高频 ModifyStat 写入与按 Tag 弱引用订阅广播热路径
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameStatModifyAndBroadcastPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Component = NewObject<UGameStatComponent>();
    UGameStatComponent* ListenerOwner = NewObject<UGameStatComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Core.ModifyBroadcast")));
    Component->RegisterStat(HealthTag, 100.0f, 1000.0f, 0.0f);

    volatile int32 ReceivedCount = 0;
    Component->SubscribeStatChanged(
        ListenerOwner,
        HealthTag,
        [&ReceivedCount](const FGameplayTag&, float, float, float) {
            ReceivedCount = ReceivedCount + 1;
        }
    );

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StatModifyBroadcastIterations,
        [Component, HealthTag](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const float Delta = (Index & 1) == 0 ? -1.0f : 1.0f;
                Component->ModifyStat(HealthTag, Delta);
            }
        }
    );

    TestTrue(FString::Printf(TEXT("StatModifyAndBroadcast %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, StatModifyBroadcastBudgetNsPerOp),
        Metrics.MedianNs < StatModifyBroadcastBudgetNsPerOp);
    TestTrue(TEXT("订阅者持续收到变更通知"), ReceivedCount > StatModifyBroadcastIterations);
    EmitBenchmarkReport(*this, TEXT("StatModifyAndBroadcast"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatAcknowledgedDiffPerfTest,
    "Game.GameplayCore.Perf.AcknowledgedDiff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 5：客户端网络复制差分计算器 (TSortedVectorMap + FastLess 整型二分) 8 键判定与记录吞吐
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameStatAcknowledgedDiffPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameStatAcknowledgedDiff Diff;
    TArray<FGameplayTag> TestTags;
    TestTags.Reserve(8);
    for (int32 Index = 0; Index < 8; ++Index) {
        TestTags.Add(UGameplayTagsManager::Get().AddNativeGameplayTag(*FString::Printf(TEXT("Perf.Diff.Stat_%d"), Index)));
    }

    volatile int32 BroadcastCount = 0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        AcknowledgedDiffIterations,
        [&Diff, &TestTags, &BroadcastCount](int32 Iterations) {
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

    TestTrue(FString::Printf(TEXT("AcknowledgedDiff %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, AcknowledgedDiffBudgetNsPerOp),
        Metrics.MedianNs < AcknowledgedDiffBudgetNsPerOp);
    TestTrue(TEXT("差分判定应有效触发记录"), BroadcastCount > 0);
    EmitBenchmarkReport(*this, TEXT("AcknowledgedDiff"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalStateTransitionPerfTest,
    "Game.GameplayCore.Perf.VitalStateTransition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 6：生命组件扣血致死与复活状态跃迁闭环热路径
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameVitalStateTransitionPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>();
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Core.VitalHealth")));
    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    VitalComp->BindStatComponent(StatComp);
    VitalComp->SetPrimaryVitalTag(HealthTag);

    volatile int32 Transitions = 0;
    VitalComp->OnVitalStateChangedNative.AddLambda([&Transitions](bool, const FGameplayTag&) {
        Transitions = Transitions + 1;
    });

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        VitalTransitionIterations,
        [StatComp, VitalComp, HealthTag](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                StatComp->SetStatCurrent(HealthTag, 0.0f);
                VitalComp->ReviveVital(1.0f);
            }
        }
    );

    TestTrue(FString::Printf(TEXT("VitalStateTransition %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, VitalTransitionBudgetNsPerOp),
        Metrics.MedianNs < VitalTransitionBudgetNsPerOp);
    TestTrue(TEXT("生死状态跃迁计数有效"), Transitions > VitalTransitionIterations);
    EmitBenchmarkReport(*this, TEXT("VitalStateTransition"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffStackAndTickPerfTest,
    "Game.GameplayCore.Perf.BuffStackAndTick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 7：Buff 组件施加、倒计时推进与自然过期微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameBuffStackAndTickPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(Host.Get());
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
        [BuffComp, &Spec, BuffTag, &CompletedOps](int32 Iterations) {
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

    TestTrue(FString::Printf(TEXT("BuffStackAndTick %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BuffStackTickBudgetNsPerOp),
        Metrics.MedianNs < BuffStackTickBudgetNsPerOp);
    TestTrue(TEXT("Buff 循环到期计数有效"), CompletedOps > 0);
    EmitBenchmarkReport(*this, TEXT("BuffStackAndTick"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffRefreshWithStatPerfTest,
    "Game.GameplayCore.Perf.BuffRefreshWithStat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 8：带属性修正器的 Buff 满层刷新免重建与原地叠层更新热路径
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameBuffRefreshWithStatPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>();
    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>();
    BuffComp->BindStatComponent(StatComp);

    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Core.BuffAtk")));
    const FGameplayTag BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Core.RefreshBuff")));
    StatComp->RegisterStat(AttackTag, 100.0f, 1000.0f, 0.0f);

    FGameBuffSpec Spec;
    Spec.BuffTag = BuffTag;
    Spec.Duration = 10.0f;
    Spec.MaxStacks = 3;
    Spec.StackPolicy = EGameBuffStackPolicy::RefreshDuration;
    FGameStatModifierSpec ModSpec;
    ModSpec.StatTag = AttackTag;
    ModSpec.Modifier = FGameStatModifier(15.0f, EGameStatModifierOp::Add);
    Spec.Modifiers.Add(ModSpec);

    BuffComp->ApplyBuff(Spec);
    BuffComp->ApplyBuff(Spec);
    BuffComp->ApplyBuff(Spec);

    volatile float Sink = 0.0f;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BuffRefreshWithStatIterations,
        [BuffComp, StatComp, &Spec, AttackTag, &Sink](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                BuffComp->ApplyBuff(Spec);
                Sink = Sink + StatComp->GetStatEffectiveCurrent(AttackTag);
            }
        }
    );

    TestTrue(FString::Printf(TEXT("BuffRefreshWithStat %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BuffRefreshWithStatBudgetNsPerOp),
        Metrics.MedianNs < BuffRefreshWithStatBudgetNsPerOp);
    TestTrue(TEXT("满层刷新有效值累加正确"), Sink > 0.0f);
    EmitBenchmarkReport(*this, TEXT("BuffRefreshWithStat"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffGrantedTagsQueryPerfTest,
    "Game.GameplayCore.Perf.BuffGrantedTagsQuery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 9：多活跃 Buff 下零拷贝 HasGrantedTag 高频状态查询热路径
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameBuffGrantedTagsQueryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>();
    const FGameplayTag TargetStateTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.State.Stun")));

    for (int32 Index = 0; Index < 6; ++Index) {
        FGameBuffSpec Spec;
        Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(*FString::Printf(TEXT("Perf.Buff.Grant_%d"), Index));
        Spec.GrantedTags.AddTag(UGameplayTagsManager::Get().AddNativeGameplayTag(*FString::Printf(TEXT("Perf.State.Tag_%d"), Index)));
        if (Index == 5) {
            Spec.GrantedTags.AddTag(TargetStateTag);
        }
        BuffComp->ApplyBuff(Spec);
    }

    volatile int32 Hits = 0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BuffGrantedTagQueryIterations,
        [BuffComp, TargetStateTag, &Hits](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                if (BuffComp->HasGrantedTag(TargetStateTag)) {
                    Hits = Hits + 1;
                }
            }
        }
    );

    TestTrue(FString::Printf(TEXT("BuffGrantedTagsQuery %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BuffGrantedTagQueryBudgetNsPerOp),
        Metrics.MedianNs < BuffGrantedTagQueryBudgetNsPerOp);
    TestTrue(TEXT("状态标签查询持续命中"), Hits > 0);
    EmitBenchmarkReport(*this, TEXT("BuffGrantedTagsQuery"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffReplicatedDiffPerfTest,
    "Game.GameplayCore.Perf.BuffReplicatedDiff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 10：客户端 FastArray 复制差分（TSortedVectorMap + FastLess 零堆分配）吞吐
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGameBuffReplicatedDiffPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>();
    TArray<FGameBuffInstance> Items;
    Items.Reserve(6);
    for (int32 Index = 0; Index < 6; ++Index) {
        FGameBuffInstance& Item = Items.AddDefaulted_GetRef();
        Item.InstanceId = Index + 1;
        Item.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(*FString::Printf(TEXT("Perf.Buff.Diff_%d"), Index));
        Item.Stacks = 1;
    }

    volatile int32 DiffEvents = 0;
    BuffComp->OnBuffChangedNative.AddLambda([&DiffEvents](const FGameplayTag&, int32) {
        DiffEvents = DiffEvents + 1;
    });

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BuffReplicatedDiffIterations,
        [BuffComp, &Items](int32 Iterations) {
            for (int32 Iter = 0; Iter < Iterations; ++Iter) {
                Items[0].Stacks = (Iter & 1) + 1;
                BuffComp->PublishReplicatedDiff(Items);
            }
        }
    );

    TestTrue(FString::Printf(TEXT("BuffReplicatedDiff %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BuffReplicatedDiffBudgetNsPerOp),
        Metrics.MedianNs < BuffReplicatedDiffBudgetNsPerOp);
    TestTrue(TEXT("差分广播事件计数有效"), DiffEvents > 0);
    EmitBenchmarkReport(*this, TEXT("BuffReplicatedDiff"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageDispatchPerfTest,
    "Game.GameplayCore.Perf.DamageDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 11：伤害上下文装配与 IDamageable 接口同步派发吞吐
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
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
        [Target, &Context, &TotalDispatched](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const FGameDamageResult Result = IDamageable::Execute_ApplyDamageContext(Target, Context);
                TotalDispatched = TotalDispatched + Result.GetActualDamage();
            }
        }
    );

    TestTrue(FString::Printf(TEXT("DamageDispatch %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, DamageDispatchBudgetNsPerOp),
        Metrics.MedianNs < DamageDispatchBudgetNsPerOp);
    TestTrue(TEXT("伤害派发实际数值应大于零"), TotalDispatched > 0.0f);
    EmitBenchmarkReport(*this, TEXT("DamageDispatch"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGamePoolableLifecyclePerfTest,
    "Game.GameplayCore.Perf.PoolableReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 12：三大战斗组件通过 IGamePoolable 执行静默入池重置与出库激活吞吐
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算返回 true
 */
bool FGamePoolableLifecyclePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    if (!Host.IsValidHost()) {
        return false;
    }

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Poolable.Health")));
    const FGameplayTag BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Perf.Poolable.Buff")));

    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(Host.Get());
    StatComp->RegisterComponent();

    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(Host.Get());
    VitalComp->BindStatComponent(StatComp);
    VitalComp->SetPrimaryVitalTag(HealthTag);
    VitalComp->RegisterComponent();

    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(Host.Get());
    BuffComp->RegisterComponent();
    BuffComp->BindStatComponent(StatComp);

    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);

    FGameBuffSpec Spec;
    Spec.BuffTag = BuffTag;
    Spec.Duration = 10.0f;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PoolableResetIterations,
        [StatComp, VitalComp, BuffComp, &Spec, HealthTag](int32 Iterations) {
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

    TestTrue(FString::Printf(TEXT("PoolableReset %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PoolableResetBudgetNsPerOp),
        Metrics.MedianNs < PoolableResetBudgetNsPerOp);
    EmitBenchmarkReport(*this, TEXT("PoolableReset"), Metrics);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
