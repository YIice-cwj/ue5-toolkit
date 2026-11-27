/**
 * @file GameFeedbackPerformanceTest.cpp
 * @brief GameFeedback 全组件热路径 10 项微基准性能回归测试套件（2 轮预热 + 7 轮采样，统计 Median/Mean/Min/Max/CV%/Mops）
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/PackageMapClient.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackPerspective.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "Logging/GameLogCategories.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Network/GameFeedbackEventNet.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "Tests/GameFeedbackTestHelpers.h"
#include "Time/FActorHitStopTable.h"
#include "Time/FGlobalDilationMixer.h"
#include "Tracker/GameFeedbackHitStopLimiter.h"
#include "Tracker/GameFeedbackPredictionHistory.h"
#include "Tracker/GameFeedbackShakeTracker.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 7;

constexpr int32 FastContainerIterations = 20000;
constexpr int32 RegistryIterations = 10000;
constexpr int32 IdentityIterations = 30000;
constexpr int32 NetSerializeIterations = 2000;
constexpr int32 ActorHitStopIterations = 10000;
constexpr int32 PerspectiveIterations = 30000;
constexpr int32 ShakeIterations = 20000;
constexpr int32 BusBroadcastIterations = 10000;

constexpr double PredictionHistoryBudgetNsPerOp = 2000.0;
constexpr double HitStopLimiterBudgetNsPerOp = 3000.0;
constexpr double RegistryLookupBudgetNsPerOp = 5000.0;
constexpr double DefinitionIdentityBudgetNsPerOp = 1000.0;
constexpr double NetSerializeBudgetNsPerOp = 15000.0;
constexpr double DilationMixerBudgetNsPerOp = 800.0;
constexpr double ActorHitStopTableBudgetNsPerOp = 2000.0;
constexpr double PerspectiveBudgetNsPerOp = 1000.0;
constexpr double ShakeTrackerBudgetNsPerOp = 2500.0;
constexpr double BusBroadcastBudgetNsPerOp = 5000.0;

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮预热与采样微基准，消除预热偏差并统计中位数、均值、极值、离散度与吞吐量
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热轮数（丢弃）
 * @param SampleRounds 正式采样轮数
 * @param IterationsPerRound 每轮迭代次数
 * @param Body 待执行测量闭包
 * @return 统计指标结构体
 */
template <typename TFunc>
FBenchmarkMetrics RunFeedbackBenchmark(
    int32 WarmupRounds,
    int32 SampleRounds,
    int32 IterationsPerRound,
    TFunc&& Body) {
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
 * @brief 统一将微基准统计结果输出至 LogGameFeedback Display 日志与自动化测试 Info
 * @param Test 自动化测试实例引用
 * @param BenchName 基准项名称
 * @param Metrics 实测指标
 */
void EmitFeedbackBenchmarkReport(FAutomationTestBase& Test, const TCHAR* BenchName, const FBenchmarkMetrics& Metrics) {
    const FString Report = FString::Printf(
        TEXT("[GameFeedbackPerf] %s: Median=%.2f ns/op (Mean=%.2f, Min=%.2f, Max=%.2f, CV=%.2f%%, %.2f Mops/s)"),
        BenchName,
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.MinNs,
        Metrics.MaxNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6);
    GAME_LOG(Display, TEXT("%s"), *Report);
    Test.AddInfo(Report);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackPredictionHistoryPerfTest,
    "Game.Feedback.Perf.PredictionHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 1. 本地预测历史队列登记与消费匹配微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且匹配正常返回 true
 */
bool FGameFeedbackPredictionHistoryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* Attacker = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    int64 MatchCount = 0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FastContainerIterations,
        [Attacker, Target, &MatchCount](int32 Iterations) {
            FGameFeedbackPredictionHistory History;
            const double BaseTime = 100.0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const uint16 EventId = static_cast<uint16>((Index % 500) + 1);
                History.Add(EventId, Attacker, Target, BaseTime + Index * 0.001);
                if (History.ConsumeMatch(EventId, Attacker, Target, BaseTime + Index * 0.001 + 0.0001)) {
                    ++MatchCount;
                }
            }
        });

    EmitFeedbackBenchmarkReport(*this, TEXT("PredictionHistory"), Metrics);
    TestTrue(
        FString::Printf(TEXT("PredictionHistory 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PredictionHistoryBudgetNsPerOp),
        Metrics.MedianNs < PredictionHistoryBudgetNsPerOp);
    TestEqual(TEXT("全部预测条目均应成功匹配消费"), MatchCount, static_cast<int64>(DefaultWarmupRounds + DefaultSampleRounds) * FastContainerIterations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackHitStopLimiterPerfTest,
    "Game.Feedback.Perf.HitStopLimiter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 2. 顿帧限流器冷却判断、累积时长申请与节流清理微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且累加有效返回 true
 */
bool FGameFeedbackHitStopLimiterPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* Attacker = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    FGameFeedbackHitStopAction Action;
    Action.CooldownInterval = 0.01f;
    Action.MaxCumulativeDuration = 1.0f;
    Action.ResetWindow = 0.5f;
    Action.VictimDuration = 0.05f;

    double TotalDuration = 0.0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FastContainerIterations,
        [Attacker, Target, &Action, &TotalDuration](int32 Iterations) {
            FGameFeedbackHitStopLimiter Limiter;
            double CurrentTime = 0.0;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                CurrentTime += 0.02;
                Limiter.PruneIfNeeded(CurrentTime);
                if (Limiter.PassCooldown(Attacker, Target, Action, CurrentTime)) {
                    TotalDuration += Limiter.AcquireDuration(Target, Action.VictimDuration, Action, CurrentTime);
                }
            }
        });

    EmitFeedbackBenchmarkReport(*this, TEXT("HitStopLimiter"), Metrics);
    TestTrue(
        FString::Printf(TEXT("HitStopLimiter 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, HitStopLimiterBudgetNsPerOp),
        Metrics.MedianNs < HitStopLimiterBudgetNsPerOp);
    TestTrue(TEXT("顿帧时长累加应大于零"), TotalDuration > 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackRegistryLookupPerfTest,
    "Game.Feedback.Perf.RegistryLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 3. 反馈注册表四级路由索引检索微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且全命中返回 true
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

    int64 ValidHits = 0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RegistryIterations,
        [Registry, SlashChannel, FireChannel, BluntChannel, &TestTags, &ValidHits](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const FGameplayTag& Channel = (Index % 3 == 0) ? SlashChannel : ((Index % 3 == 1) ? FireChannel : BluntChannel);
                const TSoftObjectPtr<UGameFeedbackDefinition> Resolved = Registry->ResolveDefinitionPtr(Channel, TestTags);
                if (!Resolved.IsNull()) {
                    ++ValidHits;
                }
            }
        });

    EmitFeedbackBenchmarkReport(*this, TEXT("RegistryLookup"), Metrics);
    TestTrue(
        FString::Printf(TEXT("RegistryLookup 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, RegistryLookupBudgetNsPerOp),
        Metrics.MedianNs < RegistryLookupBudgetNsPerOp);
    TestEqual(TEXT("注册表检索应全部命中有效资产指针"), ValidHits, static_cast<int64>(DefaultWarmupRounds + DefaultSampleRounds) * RegistryIterations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDefinitionIdentityPerfTest,
    "Game.Feedback.Perf.DefinitionIdentityLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 4. Provider 指针缓存与 Registry 稳定网络 ID 往返查找微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且查找全部命中返回 true
 */
bool FGameFeedbackDefinitionIdentityPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedFeedbackSettingsGuard SettingsGuard;
    UGameFeedbackSettings* Settings = SettingsGuard.Get();

    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* DefA = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/PerfId/DefA.DefA"));
    UGameFeedbackDefinition* DefB = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/PerfId/DefB.DefB"));
    const FGameplayTag TagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.PerfId.A")));
    const FGameplayTag TagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.PerfId.B")));
    Registry->ExactChannelMap.Add(TagA, DefA);
    Registry->ExactChannelMap.Add(TagB, DefB);
    Registry->RebuildDefinitionIndex();
    Settings->GlobalRegistry = Registry;

    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    UGameFeedbackDefinitionProvider* Provider = World ? World->GetSubsystem<UGameFeedbackDefinitionProvider>() : nullptr;
    TestNotNull(TEXT("Provider 有效"), Provider);
    if (!Provider) {
        return false;
    }

    uint64 IdAccumulator = 0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IdentityIterations,
        [Provider, DefA, DefB, &IdAccumulator](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const UGameFeedbackDefinition* CurrentDef = (Index & 1) == 0 ? DefA : DefB;
                IdAccumulator += Provider->GetDefinitionId(CurrentDef);
            }
        });

    EmitFeedbackBenchmarkReport(*this, TEXT("DefinitionIdentityLookup"), Metrics);
    TestTrue(
        FString::Printf(TEXT("DefinitionIdentityLookup 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, DefinitionIdentityBudgetNsPerOp),
        Metrics.MedianNs < DefinitionIdentityBudgetNsPerOp);
    TestTrue(TEXT("网络 ID 缓存查询应返回非零累计值"), IdAccumulator > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackNetSerializePerfTest,
    "Game.Feedback.Perf.NetEventSerializeRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 5. 网络紧凑报文 FromSignal + NetSerialize (Write/Read) + ToSignal 完整往返微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且往返成功返回 true
 */
bool FGameFeedbackNetSerializePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UDemoNetDriver* Driver = NewObject<UDemoNetDriver>(GetTransientPackage());
    Driver->World = World;
    USimulatedClientNetConnection* Connection = NewObject<USimulatedClientNetConnection>(Driver);
    Connection->Driver = Driver;
    Connection->SetInternalAck(true);
    TSharedRef<FNetGUIDCache> GuidCache = MakeShared<FNetGUIDCache>(Driver);
    UPackageMapClient* PackageMap = NewObject<UPackageMapClient>(Connection);
    PackageMap->Initialize(Connection, GuidCache);
    Connection->PackageMap = PackageMap;
    ON_SCOPE_EXIT {
        GameFeedbackTestHelpers::CleanupSimulatedConnection(Connection);
        GameFeedbackTestHelpers::CleanupTestDemoNetDriver(Driver);
    };

    AActor* Instigator = World->SpawnActor<AActor>();
    AActor* Target = World->SpawnActor<AActor>();

    FGameFeedbackSignal Signal;
    Signal.Instigator = Instigator;
    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(120.0f, 45.0f, 80.0f);
    Signal.HitResult.ImpactNormal = FVector::UpVector;
    Signal.ImpactDirection = FVector::ForwardVector;
    Signal.TraceDirection = FVector::ForwardVector;
    Signal.Magnitude = 85.0f;
    Signal.bLethal = true;
    Signal.PredictionEventId = 123;

    int64 SuccessCount = 0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        NetSerializeIterations,
        [PackageMap, &Signal, &SuccessCount](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FGameFeedbackEventNet OutEvent = FGameFeedbackEventNet::FromSignal(Signal, 0x11223344u, 0x55667788u);
                FNetBitWriter Writer(PackageMap, 1024);
                bool bWriteOk = false;
                OutEvent.NetSerialize(Writer, PackageMap, bWriteOk);

                FGameFeedbackEventNet InEvent;
                FNetBitReader Reader(PackageMap, Writer.GetData(), Writer.GetNumBits());
                bool bReadOk = false;
                InEvent.NetSerialize(Reader, PackageMap, bReadOk);

                const FGameFeedbackSignal Restored = FGameFeedbackEventNet::ToSignal(InEvent);
                if (bWriteOk && bReadOk && Restored.PredictionEventId == Signal.PredictionEventId) {
                    ++SuccessCount;
                }
            }
        });

    EmitFeedbackBenchmarkReport(*this, TEXT("NetEventSerializeRoundTrip"), Metrics);
    TestTrue(
        FString::Printf(TEXT("NetEventSerializeRoundTrip 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, NetSerializeBudgetNsPerOp),
        Metrics.MedianNs < NetSerializeBudgetNsPerOp);
    TestEqual(TEXT("全部往返序列化应成功还原"), SuccessCount, static_cast<int64>(DefaultWarmupRounds + DefaultSampleRounds) * NetSerializeIterations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDilationMixerPerfTest,
    "Game.Feedback.Perf.DilationMixer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 6. 全局分层时间膨胀混音器步进与重算微基准（静态零堆分配）
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且累加有效返回 true
 */
bool FGameFeedbackDilationMixerPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    double SumDilation = 0.0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        FastContainerIterations,
        [&SumDilation](int32 Iterations) {
            FGlobalDilationMixer Mixer;
            Mixer.Request(ETimeDilationChannel::SlowMo, 1.0f, 0.5f, 0.2f);
            Mixer.Request(ETimeDilationChannel::HitStop, 0.05f, 0.05f, 0.02f);

            for (int32 Index = 0; Index < Iterations; ++Index) {
                Mixer.Advance(0.016f);
                if (Index % 10 == 0) {
                    Mixer.Request(ETimeDilationChannel::HitStop, 0.05f, 0.05f, 0.02f);
                }
                SumDilation += Mixer.GetEffectiveDilation();
            }
        });

    EmitFeedbackBenchmarkReport(*this, TEXT("DilationMixer"), Metrics);
    TestTrue(
        FString::Printf(TEXT("DilationMixer 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, DilationMixerBudgetNsPerOp),
        Metrics.MedianNs < DilationMixerBudgetNsPerOp);
    TestTrue(TEXT("时间混音器有效缩放累加值应大于零"), SumDilation > 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackActorHitStopTablePerfTest,
    "Game.Feedback.Perf.ActorHitStopTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 7. Actor 局部顿帧表 Request / 合并 / Advance / Clear 微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且状态一致返回 true
 */
bool FGameFeedbackActorHitStopTablePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    constexpr int32 ActorPoolSize = 8;
    TArray<AActor*> Actors;
    Actors.Reserve(ActorPoolSize);
    for (int32 Index = 0; Index < ActorPoolSize; ++Index) {
        Actors.Add(World->SpawnActor<AActor>());
    }

    int64 ActiveChecks = 0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ActorHitStopIterations,
        [&Actors, &ActiveChecks](int32 Iterations) {
            FActorHitStopTable Table;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                AActor* Actor = Actors[Index % ActorPoolSize];
                Table.Request(*Actor, 0.05f, 0.01f);
                if (Table.IsActive(Actor)) {
                    ++ActiveChecks;
                }
                if ((Index & 7) == 7) {
                    Table.Advance(0.06f);
                }
            }
            Table.ClearAll();
        });

    EmitFeedbackBenchmarkReport(*this, TEXT("ActorHitStopTable"), Metrics);
    TestTrue(
        FString::Printf(TEXT("ActorHitStopTable 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ActorHitStopTableBudgetNsPerOp),
        Metrics.MedianNs < ActorHitStopTableBudgetNsPerOp);
    TestEqual(TEXT("全部顿帧请求均应处于活跃态"), ActiveChecks, static_cast<int64>(DefaultWarmupRounds + DefaultSampleRounds) * ActorHitStopIterations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackPerspectivePerfTest,
    "Game.Feedback.Perf.PerspectiveResolution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 8. 本地观察视角判定（多层所有权链与自伤分流）微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且结果正确返回 true
 */
bool FGameFeedbackPerspectivePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    APlayerController* LocalPC = World->SpawnActor<APlayerController>();
    APawn* LocalPawn = World->SpawnActor<APawn>();
    LocalPC->Possess(LocalPawn);

    AActor* Weapon = World->SpawnActor<AActor>();
    AActor* Projectile = World->SpawnActor<AActor>();
    AActor* Enemy = World->SpawnActor<AActor>();
    Weapon->SetOwner(LocalPawn);
    Projectile->SetOwner(Weapon);

    int64 AttackerHits = 0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        PerspectiveIterations,
        [LocalPC, LocalPawn, Projectile, Enemy, &AttackerHits](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const EGameFeedbackPerspective Result = GameFeedback::ResolvePerspective(LocalPC, LocalPawn, Projectile, Enemy);
                if (Result == EGameFeedbackPerspective::LocalAttacker) {
                    ++AttackerHits;
                }
            }
        });

    LocalPC->UnPossess();
    EmitFeedbackBenchmarkReport(*this, TEXT("PerspectiveResolution"), Metrics);
    TestTrue(
        FString::Printf(TEXT("PerspectiveResolution 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, PerspectiveBudgetNsPerOp),
        Metrics.MedianNs < PerspectiveBudgetNsPerOp);
    TestEqual(TEXT("多层归属链应 100% 判定为 LocalAttacker"), AttackerHits, static_cast<int64>(DefaultWarmupRounds + DefaultSampleRounds) * PerspectiveIterations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackShakeTrackerPerfTest,
    "Game.Feedback.Perf.ShakeTrackerArbitration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 9. 镜头震动追踪器同帧振幅仲裁与弱振幅快速抑制微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且状态一致返回 true
 */
bool FGameFeedbackShakeTrackerPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    APlayerCameraManager* CameraManager = World->SpawnActor<APlayerCameraManager>();
    FGameFeedbackShakeTracker Tracker;
    ++GFrameCounter;
    Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 10.0f, FVector::ForwardVector, true);

    int64 TrackedSum = 0;
    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ShakeIterations,
        [&Tracker, CameraManager, &TrackedSum](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Tracker.Execute(CameraManager, UCameraShakeBase::StaticClass(), 1.0f, FVector::ForwardVector, true);
                TrackedSum += Tracker.GetTrackedCount();
            }
        });

    Tracker.StopAll();
    EmitFeedbackBenchmarkReport(*this, TEXT("ShakeTrackerArbitration"), Metrics);
    TestTrue(
        FString::Printf(TEXT("ShakeTrackerArbitration 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, ShakeTrackerBudgetNsPerOp),
        Metrics.MedianNs < ShakeTrackerBudgetNsPerOp);
    TestEqual(TEXT("同帧弱振幅仲裁始终维持单条追踪记录"), TrackedSum, static_cast<int64>(DefaultWarmupRounds + DefaultSampleRounds) * ShakeIterations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackBusEndToEndPerfTest,
    "Game.Feedback.Perf.BusEndToEndBroadcast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 10. BroadcastFeedback 预解析 + 消息总线端到端广播吞吐微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 耗时低于预算且全部消息送达返回 true
 */
bool FGameFeedbackBusEndToEndPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedFeedbackSettingsGuard SettingsGuard;
    UGameFeedbackSettings* Settings = SettingsGuard.Get();
    Settings->bEnableFeedback = true;

    UGameFeedbackDefinition* Def = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/PerfBus/Def.Def"));
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.PerfBus.Slash")));
    Registry->ExactChannelMap.Add(Channel, Def);
    Registry->RebuildDefinitionIndex();
    Settings->GlobalRegistry = Registry;

    GameFeedbackTestHelpers::FScopedStandaloneFeedbackWorld TestEnv(FName(TEXT("GameFeedbackBusPerfWorld")));
    UWorld* World = TestEnv.GetWorld();
    UGameplayMessageSubsystem* Bus = TestEnv.GetGameInstance()->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("测试世界有效"), World);
    TestNotNull(TEXT("消息总线有效"), Bus);
    if (!World || !Bus) {
        return false;
    }

    int64 ReceivedCount = 0;
    FGameplayMessageListenerHandle Handle = Bus->RegisterListener<FGameFeedbackMessage>(
        TAG_Feedback_Channel_Play,
        [&ReceivedCount]([[maybe_unused]] FGameplayTag InChannel, const FGameFeedbackMessage& Msg) {
            if (Msg.OverrideDefinition != nullptr) {
                ++ReceivedCount;
            }
        });
    ON_SCOPE_EXIT {
        Handle.Unregister();
    };

    FGameFeedbackSignal Signal;
    Signal.PrimaryChannel = Channel;
    Signal.Magnitude = 50.0f;

    const FBenchmarkMetrics Metrics = RunFeedbackBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        BusBroadcastIterations,
        [World, &Signal](int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                UGameFeedbackBlueprintLibrary::BroadcastFeedback(World, Signal);
            }
        });

    EmitFeedbackBenchmarkReport(*this, TEXT("BusEndToEndBroadcast"), Metrics);
    TestTrue(
        FString::Printf(TEXT("BusEndToEndBroadcast 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), Metrics.MedianNs, BusBroadcastBudgetNsPerOp),
        Metrics.MedianNs < BusBroadcastBudgetNsPerOp);
    TestEqual(
        TEXT("全部广播均应完成预解析并准确送达监听者"),
        ReceivedCount,
        static_cast<int64>(DefaultWarmupRounds + DefaultSampleRounds) * BusBroadcastIterations);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
