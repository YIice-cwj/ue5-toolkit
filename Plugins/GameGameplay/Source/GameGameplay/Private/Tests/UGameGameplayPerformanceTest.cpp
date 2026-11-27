/**
 * @file UGameGameplayPerformanceTest.cpp
 * @brief GameGameplay 全组件热路径 12 项微基准性能测试套件（2 轮预热 + 7 轮正式采样，输出 Median/Mean/CV%/Mops）
 *
 * 遵循通用代码规范 10/03 §5 与 UE5 性能基准规范：多轮预热与采样消除冷启动偏差，
 * 同步输出 LogGameGameplay Display 日志与 Automation Info。
 */

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Ability/UGameAbilitySystemBootstrap.h"
#include "Ability/UGameAbilitySystemComponent.h"
#include "Ability/UGameGameplayAbility.h"
#include "AbilitySystemTestAttributeSet.h"
#include "Async/ParallelFor.h"
#include "Attribute/UGameBaseAttributeSet.h"
#include "Attribute/UGameplayAttributeValueMapping.h"
#include "Bridge/UGameFeedbackAdapter.h"
#include "Bridge/UGameGasBuffBridge.h"
#include "Core/IGamePoolable.h"
#include "Damage/FDamageChannelBinding.h"
#include "Damage/FDamageModifier.h"
#include "Damage/FGameDamageChannelRegistry.h"
#include "Damage/FGameGameplayEffectContext.h"
#include "Damage/UGameDamageExecution.h"
#include "Engine/World.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "Logging/GameLogCategories.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 7;

constexpr int32 ChannelLookupIterations = 50000;
constexpr double ChannelLookupBudgetNs = 500.0;

constexpr int32 ConcurrentReadWorkers = 8;
constexpr int32 ConcurrentReadIterationsPerWorker = 4000;
constexpr double ConcurrentReadBudgetNs = 1500.0;

constexpr int32 ModifierEvalIterations = 200000;
constexpr double ModifierEvalBudgetNs = 50.0;

constexpr int32 EffectContextCompactIterations = 10000;
constexpr double EffectContextCompactBudgetNs = 10000.0;

constexpr int32 EffectContextFullIterations = 5000;
constexpr double EffectContextFullBudgetNs = 20000.0;

constexpr int32 DamagePipelineIterations = 5000;
constexpr double DamagePipelineBudgetNs = 20000.0;

constexpr int32 AbilityCanActivateIterations = 20000;
constexpr double AbilityCanActivateBudgetNs = 3000.0;

constexpr int32 BaseAttributeOpsIterations = 10000;
constexpr double BaseAttributeOpsBudgetNs = 4000.0;

constexpr int32 ValueMappingBroadcastIterations = 10000;
constexpr double ValueMappingBroadcastBudgetNs = 4000.0;

constexpr int32 GasBuffBridgeIterations = 2000;
constexpr double GasBuffBridgeBudgetNs = 30000.0;

constexpr int32 FeedbackAdapterIterations = 20000;
constexpr double FeedbackAdapterBudgetNs = 3000.0;

constexpr int32 PoolableResetIterations = 10000;
constexpr double PoolableResetBudgetNs = 10000.0;

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
 * @param OpsPerRound 每轮计入分母的操作总数
 * @param Body 待测闭包（接收单轮操作计数）
 * @return 统计指标结构体
 */
template <typename TFunc>
FBenchmarkMetrics RunBenchmark(
    int32 WarmupRounds,
    int32 SampleRounds,
    int32 OpsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(OpsPerRound);
    }

    TArray<double> SamplesNs;
    SamplesNs.Reserve(SampleRounds);

    double SumNs = 0.0;
    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const double StartTime = FPlatformTime::Seconds();
        Body(OpsPerRound);
        const double ElapsedSec = FPlatformTime::Seconds() - StartTime;
        const double NsPerOp = (ElapsedSec * 1.0e9) / static_cast<double>(OpsPerRound);
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
 * @brief 将微基准统计报告同时写入 LogGameGameplay Display 日志与 Automation Info
 * @param Test 当前自动化测试实例引用
 * @param BenchName 基准测试名称
 * @param Metrics 实测统计指标
 */
void EmitBenchmarkReport(FAutomationTestBase& Test, const TCHAR* BenchName, const FBenchmarkMetrics& Metrics) {
    const FString Report = FString::Printf(
        TEXT("[GameGameplayPerf] %s: Median=%.1f ns/op (Mean=%.1f, Min=%.1f, Max=%.1f, CV=%.2f%%, %.2f Mops/s)"),
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
    FGameGameplayChannelRegistryPerfTest,
    "Game.Gameplay.Perf.ChannelRegistryLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 1：伤害通道注册表读锁 + 栈内联快照热路径查询吞吐
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayChannelRegistryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag TagPhys = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.Phys")));
    const FGameplayTag TagFire = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.Fire")));
    const FGameplayTag TagTrue = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.True")));

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));

    UClass* ExecClass = UGameDamageExecution::StaticClass();
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    ON_SCOPE_EXIT {
        Registry.UnregisterAll(ExecClass);
    };

    Registry.UnregisterAll(ExecClass);
    Registry.Register(ExecClass, FDamageChannelBinding(TagPhys, HealthAttr));
    Registry.Register(ExecClass, FDamageChannelBinding(TagFire, HealthAttr));
    Registry.Register(ExecClass, FDamageChannelBinding(TagTrue, HealthAttr));

    volatile int32 TotalBindings = 0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ChannelLookupIterations,
        [&Registry, ExecClass, &TotalBindings](int32 Iterations) {
            int32 RoundTotal = 0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Registry.ForEachBinding(ExecClass, [&RoundTotal](TConstArrayView<FDamageChannelBinding> Bindings) {
                    RoundTotal += Bindings.Num();
                });
            }
            TotalBindings = RoundTotal;
        }
    );

    TestEqual(TEXT("单轮累计绑定数符合预期"), static_cast<int32>(TotalBindings), ChannelLookupIterations * 3);
    TestTrue(
        FString::Printf(TEXT("ChannelRegistryLookup %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ChannelLookupBudgetNs),
        Metrics.MedianNs < ChannelLookupBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("ChannelRegistryLookup"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayChannelRegistryConcurrentPerfTest,
    "Game.Gameplay.Perf.ChannelRegistryConcurrentRead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 2：伤害通道注册表多线程并发读扩展性吞吐
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayChannelRegistryConcurrentPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag TagPhys = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.Conc.Phys")));
    const FGameplayTag TagFire = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.Conc.Fire")));
    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));

    UClass* ExecClass = UGameDamageExecution::StaticClass();
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    ON_SCOPE_EXIT {
        Registry.UnregisterAll(ExecClass);
    };

    Registry.UnregisterAll(ExecClass);
    Registry.Register(ExecClass, FDamageChannelBinding(TagPhys, HealthAttr));
    Registry.Register(ExecClass, FDamageChannelBinding(TagFire, HealthAttr));

    constexpr int32 TotalOpsPerRound = ConcurrentReadWorkers * ConcurrentReadIterationsPerWorker;
    std::atomic<int32> ObservedOps{0};

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        TotalOpsPerRound,
        [&Registry, ExecClass, &ObservedOps](int32) {
            ObservedOps.store(0, std::memory_order_relaxed);
            ParallelFor(ConcurrentReadWorkers, [&Registry, ExecClass, &ObservedOps](int32) {
                int32 LocalCount = 0;
                for (int32 Iter = 0; Iter < ConcurrentReadIterationsPerWorker; ++Iter) {
                    Registry.ForEachBinding(ExecClass, [&LocalCount](TConstArrayView<FDamageChannelBinding> Bindings) {
                        LocalCount += Bindings.Num();
                    });
                }
                ObservedOps.fetch_add(LocalCount, std::memory_order_relaxed);
            });
        }
    );

    TestEqual(TEXT("并发读全部命中预期 2 条通道"), ObservedOps.load(), TotalOpsPerRound * 2);
    TestTrue(
        FString::Printf(TEXT("ChannelRegistryConcurrentRead %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ConcurrentReadBudgetNs),
        Metrics.MedianNs < ConcurrentReadBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("ChannelRegistryConcurrentRead"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayDamageModifierPerfTest,
    "Game.Gameplay.Perf.DamageModifierEvaluation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 3：伤害修饰器纯数值演算吞吐
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayDamageModifierPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FDamageModifier Modifiers[4] = {
        FDamageModifier(1.25f, 15.0f),
        FDamageModifier(0.80f, -10.0f),
        FDamageModifier::MakeOverride(999.0f),
        FDamageModifier(1.0f, 0.0f)
    };

    volatile float Accumulated = 0.0f;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ModifierEvalIterations,
        [&Modifiers, &Accumulated](int32 Iterations) {
            float LocalSum = 0.0f;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const float BaseDamage = 50.0f + static_cast<float>(Index & 0x7F);
                LocalSum += Modifiers[Index & 3].Evaluate(BaseDamage);
            }
            Accumulated = LocalSum;
        }
    );

    TestTrue(TEXT("累加演算值大于零"), Accumulated > 0.0f);
    TestTrue(
        FString::Printf(TEXT("DamageModifierEvaluation %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ModifierEvalBudgetNs),
        Metrics.MedianNs < ModifierEvalBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("DamageModifierEvaluation"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayEffectContextCompactNetPerfTest,
    "Game.Gameplay.Perf.EffectContextNetSerializeCompact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 4：空/无预测事件 ID 打击上下文 5-bit 紧凑网络往返序列化吞吐
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayEffectContextCompactNetPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameGameplayEffectContext CompactSource;
    TArray<uint8> Buffer;
    int32 LastByteSize = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        EffectContextCompactIterations,
        [&CompactSource, &Buffer, &LastByteSize](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Buffer.Reset();
                FMemoryWriter Writer(Buffer);
                bool bWriteOk = true;
                CompactSource.NetSerialize(Writer, nullptr, bWriteOk);

                FMemoryReader Reader(Buffer);
                FGameGameplayEffectContext Dest;
                bool bReadOk = true;
                Dest.NetSerialize(Reader, nullptr, bReadOk);
            }
            LastByteSize = Buffer.Num();
        }
    );

    TestTrue(TEXT("紧凑空包序列化体积大于 0"), LastByteSize > 0);
    TestTrue(
        FString::Printf(TEXT("EffectContextNetSerializeCompact %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, EffectContextCompactBudgetNs),
        Metrics.MedianNs < EffectContextCompactBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("EffectContextNetSerializeCompact"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayEffectContextNetPerfTest,
    "Game.Gameplay.Perf.EffectContextNetSerializeFull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 5：全载荷打击反馈上下文（事件 ID + 伤害 + 双通道 + 标签 + 动态值）往返序列化吞吐
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayEffectContextNetPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag TagPhys = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.NetPerf.Phys")));
    const FGameplayTag TagFire = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.NetPerf.Fire")));
    const FGameplayTag TagCrit = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.NetPerf.Crit")));
    const FGameplayTag TagHitStop = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Dynamic.NetPerf.HitStop")));

    FGameGameplayEffectContext SourceContext;
    SourceContext.SetFeedbackEventId(42);
    SourceContext.SetActualDamage(150.0f);
    SourceContext.SetActualChannelDamage(TagPhys, 100.0f);
    SourceContext.SetActualChannelDamage(TagFire, 50.0f);
    SourceContext.AddFeedbackTag(TagCrit);
    SourceContext.SetDynamicValue(TagHitStop, 0.12f);

    TArray<uint8> Buffer;
    volatile float LastDamage = 0.0f;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        EffectContextFullIterations,
        [&SourceContext, &Buffer, &LastDamage](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Buffer.Reset();
                FMemoryWriter Writer(Buffer);
                bool bWriteSuccess = true;
                SourceContext.NetSerialize(Writer, nullptr, bWriteSuccess);

                FMemoryReader Reader(Buffer);
                FGameGameplayEffectContext DestContext;
                bool bReadSuccess = true;
                DestContext.NetSerialize(Reader, nullptr, bReadSuccess);
                LastDamage = DestContext.GetActualDamage();
            }
        }
    );

    TestEqual(TEXT("全载荷往返恢复实际伤害"), static_cast<float>(LastDamage), 150.0f);
    TestTrue(
        FString::Printf(TEXT("EffectContextNetSerializeFull %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, EffectContextFullBudgetNs),
        Metrics.MedianNs < EffectContextFullBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("EffectContextNetSerializeFull"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayDamageExecutionPipelinePerfTest,
    "Game.Gameplay.Perf.DamageExecutionPipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 6：完整 ExecCalc 双通道伤害管线执行吞吐
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayDamageExecutionPipelinePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* SourceActor = NewObject<AActor>(GetTransientPackage());
    AActor* TargetActor = NewObject<AActor>(GetTransientPackage());

    UGameAbilitySystemComponent* SourceASC = NewObject<UGameAbilitySystemComponent>(SourceActor);
    SourceASC->InitAbilityActorInfo(SourceActor, SourceActor);

    UGameAbilitySystemComponent* TargetASC = NewObject<UGameAbilitySystemComponent>(TargetActor);
    TargetASC->InitAbilityActorInfo(TargetActor, TargetActor);

    UAbilitySystemTestAttributeSet* TargetAttrSet = NewObject<UAbilitySystemTestAttributeSet>(TargetASC);
    TargetASC->AddAttributeSetSubobject(TargetAttrSet);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));
    TargetASC->SetNumericAttributeBase(HealthAttr, 100000.0f);

    const FGameplayTag PhysTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.PipePerf.Phys")));
    const FGameplayTag FireTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.PipePerf.Fire")));

    UClass* ExecClass = UGameDamageExecution::StaticClass();
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    ON_SCOPE_EXIT {
        Registry.UnregisterAll(ExecClass);
    };

    Registry.UnregisterAll(ExecClass);
    Registry.Register(ExecClass, FDamageChannelBinding(PhysTag, HealthAttr));
    Registry.Register(ExecClass, FDamageChannelBinding(FireTag, HealthAttr));

    UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage());
    Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

    FGameplayEffectContextHandle ContextHandle(new FGameGameplayEffectContext());
    ContextHandle.AddInstigator(SourceActor, SourceActor);

    FGameplayEffectSpec Spec(Effect, ContextHandle, 1.0f);
    Spec.SetSetByCallerMagnitude(PhysTag, 40.0f);
    Spec.SetSetByCallerMagnitude(FireTag, 25.0f);

    FGameplayEffectCustomExecutionParameters ExecParams(
        Spec,
        TArray<FGameplayEffectExecutionScopedModifierInfo>(),
        TargetASC,
        FGameplayTagContainer(),
        FPredictionKey()
    );

    const UGameDamageExecution* ExecCalc = GetDefault<UGameDamageExecution>();
    TArray<FGameplayModifierEvaluatedData> Modifiers;
    int32 LastRoundOutputs = 0;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        DamagePipelineIterations,
        [ExecCalc, &ExecParams, &Modifiers, &LastRoundOutputs](int32 Iterations) {
            int32 OutputSum = 0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FGameplayEffectCustomExecutionOutput Output;
                ExecCalc->Execute_Implementation(ExecParams, Output);
                Modifiers.Reset();
                Output.GetOutputModifiers(Modifiers);
                OutputSum += Modifiers.Num();
            }
            LastRoundOutputs = OutputSum;
        }
    );

    TestEqual(TEXT("结算修饰器输出数量正确"), LastRoundOutputs, DamagePipelineIterations * 2);
    TestTrue(
        FString::Printf(TEXT("DamageExecutionPipeline %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, DamagePipelineBudgetNs),
        Metrics.MedianNs < DamagePipelineBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("DamageExecutionPipeline"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayAbilityCanActivatePerfTest,
    "Game.Gameplay.Perf.AbilityCanActivate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 7：UGameGameplayAbility::CanActivateAbility 阻断守卫与零拷贝标签过滤热路径
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayAbilityCanActivatePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* HostActor = NewObject<AActor>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(HostActor);
    ASC->InitAbilityActorInfo(HostActor, HostActor);

    FGameplayAbilitySpecHandle AbilityHandle;
    {
        UGameGameplayAbility::StaticClass()->ClassFlags &= ~CLASS_Abstract;
        ON_SCOPE_EXIT {
            UGameGameplayAbility::StaticClass()->ClassFlags |= CLASS_Abstract;
        };
        AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGameGameplayAbility::StaticClass(), 1, INDEX_NONE, HostActor));
    }

    FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AbilityHandle);
    UGameGameplayAbility* AbilityInstance = Spec != nullptr ? Cast<UGameGameplayAbility>(Spec->GetPrimaryInstance()) : nullptr;
    const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();
    TestNotNull(TEXT("能力实例创建成功"), AbilityInstance);
    TestNotNull(TEXT("ActorInfo 有效"), ActorInfo);
    if (AbilityInstance == nullptr || ActorInfo == nullptr) {
        return false;
    }

    FStructProperty* BlockingTagsProp = FindFProperty<FStructProperty>(UGameGameplayAbility::StaticClass(), TEXT("BlockingStateTags"));
    if (BlockingTagsProp != nullptr) {
        auto& BlockingTags = *reinterpret_cast<FGameplayTagContainer*>(BlockingTagsProp->ContainerPtrToValuePtr<void>(AbilityInstance));
        BlockingTags.AddTag(UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Perf.Dead"))));
        BlockingTags.AddTag(UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Perf.Stun"))));
    }

    volatile int32 AllowedCount = 0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        AbilityCanActivateIterations,
        [AbilityInstance, AbilityHandle, ActorInfo, &AllowedCount](int32 Iterations) {
            int32 LocalAllowed = 0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                if (AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo)) {
                    ++LocalAllowed;
                }
            }
            AllowedCount = LocalAllowed;
        }
    );

    TestEqual(TEXT("全部直通激活检查通过"), static_cast<int32>(AllowedCount), AbilityCanActivateIterations);
    TestTrue(
        FString::Printf(TEXT("AbilityCanActivate %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, AbilityCanActivateBudgetNs),
        Metrics.MedianNs < AbilityCanActivateBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("AbilityCanActivate"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayBaseAttributeSetOpsPerfTest,
    "Game.Gameplay.Perf.BaseAttributeSetOps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 8：UGameBaseAttributeSet 元属性消费归零 + 阈值闩切换 + 增量落地热路径
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayBaseAttributeSetOpsPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UAbilitySystemTestAttributeSet* AttrSet = NewObject<UAbilitySystemTestAttributeSet>(ASC);
    ASC->AddAttributeSetSubobject(AttrSet);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));
    ASC->SetNumericAttributeBase(HealthAttr, 1000.0f);

    const FGameplayTag ThresholdTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Perf.Threshold")));
    volatile float MovedSum = 0.0f;

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BaseAttributeOpsIterations,
        [ASC, HealthAttr, ThresholdTag, &MovedSum](int32 Iterations) {
            float LocalSum = 0.0f;
            FGameplayAttributeData Meta;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Meta.SetBaseValue(10.0f);
                Meta.SetCurrentValue(10.0f);
                LocalSum += UGameBaseAttributeSet::MoveMetaAttribute(Meta);
                const float Delta = (Index & 1) == 0 ? -1.0f : 1.0f;
                UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, HealthAttr, Delta);
                if (UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag)) {
                    UGameBaseAttributeSet::ResetThreshold(ASC, ThresholdTag);
                }
            }
            MovedSum = LocalSum;
        }
    );

    TestTrue(TEXT("元属性消费累加有效"), MovedSum > 0.0f);
    TestTrue(
        FString::Printf(TEXT("BaseAttributeSetOps %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BaseAttributeOpsBudgetNs),
        Metrics.MedianNs < BaseAttributeOpsBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("BaseAttributeSetOps"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayValueMappingBroadcastPerfTest,
    "Game.Gameplay.Perf.AttributeValueMappingBroadcast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 9：UGameplayAttributeValueMapping 属性变更分发到原生订阅者热路径
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayValueMappingBroadcastPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UAbilitySystemTestAttributeSet* AttrSet = NewObject<UAbilitySystemTestAttributeSet>(ASC);
    ASC->AddAttributeSetSubobject(AttrSet);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));
    ASC->SetNumericAttributeBase(HealthAttr, 100.0f);

    UGameplayAttributeValueMapping* Mapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(ASC, HealthAttr);
    TestNotNull(TEXT("属性映射创建成功"), Mapping);
    if (Mapping == nullptr) {
        return false;
    }

    volatile int32 CallbackCount = 0;
    Mapping->OnValueChangedNative.AddLambda([&CallbackCount](float, float) {
        CallbackCount = CallbackCount + 1;
    });

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ValueMappingBroadcastIterations,
        [ASC, HealthAttr](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                ASC->SetNumericAttributeBase(HealthAttr, static_cast<float>(100 + (Index & 63)));
            }
        }
    );

    Mapping->Unbind();
    TestTrue(TEXT("属性映射持续触发原生回调"), CallbackCount > ValueMappingBroadcastIterations);
    TestTrue(
        FString::Printf(TEXT("AttributeValueMappingBroadcast %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ValueMappingBroadcastBudgetNs),
        Metrics.MedianNs < ValueMappingBroadcastBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("AttributeValueMappingBroadcast"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayGasBuffBridgePerfTest,
    "Game.Gameplay.Perf.GasBuffBridgeLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 10：UGameGasBuffBridge 施加 + 激活/叠层/时长查询 + 移除完整闭环热路径
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayGasBuffBridgePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("基准世界创建成功"), World);
    if (World == nullptr) {
        return false;
    }
    ON_SCOPE_EXIT {
        World->DestroyWorld(false);
    };

    AActor* Host = World->SpawnActor<AActor>();
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(Host);
    ASC->RegisterComponent();
    ASC->InitAbilityActorInfo(Host, Host);

    UGameplayEffect* BaseCDO = GetMutableDefault<UGameplayEffect>();
    const EGameplayEffectDurationType OldPolicy = BaseCDO->DurationPolicy;
    BaseCDO->DurationPolicy = EGameplayEffectDurationType::Infinite;
    ON_SCOPE_EXIT {
        BaseCDO->DurationPolicy = OldPolicy;
    };

    volatile int32 ValidApplies = 0;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GasBuffBridgeIterations,
        [ASC, Host, &ValidApplies](int32 Iterations) {
            int32 LocalCount = 0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const FActiveGameplayEffectHandle Handle = UGameGasBuffBridge::ApplyBuffEffect(ASC, UGameplayEffect::StaticClass(), 1, Host);
                if (UGameGasBuffBridge::IsBuffEffectActive(ASC, Handle) && UGameGasBuffBridge::GetBuffStackCount(ASC, Handle) == 1) {
                    ++LocalCount;
                }
                UGameGasBuffBridge::RemoveBuffEffect(ASC, Handle);
            }
            ValidApplies = LocalCount;
        }
    );

    TestEqual(TEXT("全部 Buff 施加/查询/移除闭环成功"), static_cast<int32>(ValidApplies), GasBuffBridgeIterations);
    TestTrue(
        FString::Printf(TEXT("GasBuffBridgeLifecycle %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, GasBuffBridgeBudgetNs),
        Metrics.MedianNs < GasBuffBridgeBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("GasBuffBridgeLifecycle"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayFeedbackAdapterPerfTest,
    "Game.Gameplay.Perf.FeedbackAdapterMakeSignal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 11：UGameFeedbackAdapter::MakeSignalFromDamage 伤害契约→表现信号转换热路径
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayFeedbackAdapterPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* Instigator = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.Adapter.Slash")));
    const FGameplayTag FireTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.Adapter.Fire")));

    FGameDamageContext Context;
    Context.Instigator = Instigator;
    Context.HitResult.ImpactNormal = FVector::UpVector;
    Context.HitResult.TraceStart = FVector::ZeroVector;
    Context.HitResult.TraceEnd = FVector(100.0f, 0.0f, 0.0f);

    FGameDamageResult Result;
    Result.SetActualDamage(120.0f);
    Result.SetActualChannelDamage(SlashTag, 80.0f);
    Result.SetActualChannelDamage(FireTag, 40.0f);
    Result.AddFeedbackTag(TAG_Feedback_State_Lethal);

    volatile float SumMagnitude = 0.0f;
    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FeedbackAdapterIterations,
        [Target, &Context, &Result, &SumMagnitude](int32 Iterations) {
            float LocalSum = 0.0f;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const FGameFeedbackSignal Signal = UGameFeedbackAdapter::MakeSignalFromDamage(Target, Context, Result);
                LocalSum += Signal.Magnitude;
            }
            SumMagnitude = LocalSum;
        }
    );

    TestTrue(TEXT("转换信号伤害累加值有效"), SumMagnitude > 0.0f);
    TestTrue(
        FString::Printf(TEXT("FeedbackAdapterMakeSignal %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, FeedbackAdapterBudgetNs),
        Metrics.MedianNs < FeedbackAdapterBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("FeedbackAdapterMakeSignal"), Metrics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayPoolableComponentsPerfTest,
    "Game.Gameplay.Perf.PoolableComponentsReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准 12：Bootstrap + ASC + ValueMapping 三大池化组件 IGamePoolable 入池/出池重置往返吞吐
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayPoolableComponentsPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemBootstrap* Bootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UGameplayAttributeValueMapping* Mapping = NewObject<UGameplayAttributeValueMapping>(GetTransientPackage());

    TestNotNull(TEXT("Bootstrap 实现 IGamePoolable"), Cast<IGamePoolable>(Bootstrap));
    TestNotNull(TEXT("ASC 实现 IGamePoolable"), Cast<IGamePoolable>(ASC));
    TestNotNull(TEXT("ValueMapping 实现 IGamePoolable"), Cast<IGamePoolable>(Mapping));

    const FBenchmarkMetrics Metrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PoolableResetIterations,
        [Bootstrap, ASC, Mapping](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                IGamePoolable::Execute_OnReleaseToPool(Bootstrap);
                IGamePoolable::Execute_OnReleaseToPool(ASC);
                IGamePoolable::Execute_OnReleaseToPool(Mapping);

                IGamePoolable::Execute_OnAcquireFromPool(Bootstrap);
                IGamePoolable::Execute_OnAcquireFromPool(ASC);
                IGamePoolable::Execute_OnAcquireFromPool(Mapping);
            }
        }
    );

    TestFalse(TEXT("入池后 Bootstrap 状态洁净"), Bootstrap->IsDefaultAbilitiesGranted());
    TestFalse(TEXT("入池后 ValueMapping 状态洁净"), Mapping->IsBound());
    TestTrue(
        FString::Printf(TEXT("PoolableComponentsReset %.1f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PoolableResetBudgetNs),
        Metrics.MedianNs < PoolableResetBudgetNs
    );
    EmitBenchmarkReport(*this, TEXT("PoolableComponentsReset"), Metrics);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
