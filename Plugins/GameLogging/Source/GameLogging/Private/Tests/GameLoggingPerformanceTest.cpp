// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLoggingPerformanceTest.cpp
 * @brief GameLogging 全热路径微基准性能回归测试套件（宏分发 / 去重限频 / 注册表扩展 / 环形队列 / 子系统广播）
 *
 * 遵循通用代码规范与 UE5 性能基准规范：多轮预热与采样（>= 5 轮），统计中位数、均值、Min/Max 与变异系数 (CV%)。
 */

#include "Logging/GameLogCategories.h"
#include "Remote/GameLoggingSubsystem.h"
#include "Remote/GameRemoteLogOutputDevice.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Containers/StringConv.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Types/TCancelableScopeGuard.h"

#include <atomic>

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogPerfStaticBaseline, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogPerfDynamicTarget, Log, All);

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 MacroSuppressedIterations = 1000000;
constexpr double DynamicSuppressedBudgetNsPerOp = 30.0;

constexpr int32 OnceAndThrottleIterations = 1000000;
constexpr double OnceShortcutBudgetNsPerOp = 20.0;
constexpr double ThrottleSuppressedBudgetNsPerOp = 30.0;

constexpr int32 RegistryLookupIterations = 200000;
constexpr double RegistryLookup200BudgetNsPerOp = 150.0;

constexpr int32 RemoteFilterIterations = 1000000;
constexpr double RemoteFilterBudgetNsPerOp = 40.0;

constexpr int32 RemoteRingBatchSize = 1024;
constexpr int32 RemoteRingBatchesPerRound = 16;
constexpr double RemoteRingRoundTripBudgetNsPerOp = 600.0;

constexpr int32 RemoteMultiThreadBatchLogs = 1600;
constexpr int32 RemoteMultiThreadBatchesPerRound = 4;
constexpr double RemoteMultiThreadBudgetNsPerOp = 1500.0;

constexpr int32 SubsystemBatchLogs = 512;
constexpr double SubsystemBroadcastBudgetNsPerOp = 2000.0;

struct FLoggingBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮基准测试，消除预热偏差并统计中位数、均值、极值与变异系数 (CV%)
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热轮数（测量后丢弃）
 * @param SampleRounds 正式采样轮数（规范要求 >= 5 轮）
 * @param IterationsPerRound 每轮迭代执行次数
 * @param Body 待执行测量闭包
 * @return 统计度量结果
 */
template <typename TFunc>
FLoggingBenchmarkMetrics RunLoggingBenchmark(
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

    FLoggingBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = CvPercent;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingDispatchPerfTest,
    "Game.Logging.Perf.DynamicDispatchVsStaticDirect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 对比 GAME_LOG 槽位动态分发（抑制热路径）与原生 UE_LOG 静态直连开销微基准
 * @param Parameters 自动化测试参数（未使用）
 * @return 预算内返回 true
 */
bool FGameLoggingDispatchPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const auto RestoreGuard = MakeCancelableScopeGuard([]() {
        Game::Log::FGameLogRegistry::RegisterCategory(UE_MODULE_NAME, &LogGame);
        LogPerfDynamicTarget.SetVerbosity(ELogVerbosity::Log);
        LogPerfStaticBaseline.SetVerbosity(ELogVerbosity::Log);
    });

    Game::Log::FGameLogRegistry::RegisterCategory(UE_MODULE_NAME, &LogPerfDynamicTarget);
    LogPerfDynamicTarget.SetVerbosity(ELogVerbosity::Log);
    LogPerfStaticBaseline.SetVerbosity(ELogVerbosity::Log);

    const FLoggingBenchmarkMetrics StaticMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        MacroSuppressedIterations,
        [](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                std::atomic_signal_fence(std::memory_order_acq_rel);
                if constexpr (Game::Config::bEnableLog) {
                    UE_LOG(LogPerfStaticBaseline, Verbose, TEXT("Suppressed static log %d"), Index);
                }
            }
        }
    );

    const FLoggingBenchmarkMetrics DynamicMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        MacroSuppressedIterations,
        [](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                std::atomic_signal_fence(std::memory_order_acq_rel);
                GAME_LOG(Verbose, TEXT("Suppressed dynamic slot log %d"), Index);
            }
        }
    );

    TestTrue(
        FString::Printf(
            TEXT("GAME_LOG 槽位动态分发抑制热路径耗时 %.2f ns/op 应低于预算 %.0f ns/op"),
            DynamicMetrics.MedianNs,
            DynamicSuppressedBudgetNsPerOp
        ),
        DynamicMetrics.MedianNs < DynamicSuppressedBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("[GameLoggingPerf] Static UE_LOG (Suppressed): %.2f ns/op (CV: %.2f%%) | Dynamic Slot GAME_LOG (Suppressed): %.2f ns/op (CV: %.2f%%, %.2f Mops/s) | Overhead: +%.2f ns"),
        StaticMetrics.MedianNs,
        StaticMetrics.CvPercent,
        DynamicMetrics.MedianNs,
        DynamicMetrics.CvPercent,
        DynamicMetrics.OpsPerSec / 1.0e6,
        DynamicMetrics.MedianNs - StaticMetrics.MedianNs
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingOnceAndThrottlePerfTest,
    "Game.Logging.Perf.OnceAndThrottleHotPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GAME_LOG_ONCE / GAME_CLOG_ONCE 触发后 TTAS 短路与 GAME_LOG_THROTTLE 抑制前置短路微基准
 * @param Parameters 自动化测试参数（未使用）
 * @return 预算内返回 true
 */
bool FGameLoggingOnceAndThrottlePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const auto RestoreGuard = MakeCancelableScopeGuard([]() {
        Game::Log::FGameLogRegistry::RegisterCategory(UE_MODULE_NAME, &LogGame);
        LogPerfDynamicTarget.SetVerbosity(ELogVerbosity::Log);
    });

    Game::Log::FGameLogRegistry::RegisterCategory(UE_MODULE_NAME, &LogPerfDynamicTarget);
    LogPerfDynamicTarget.SetVerbosity(ELogVerbosity::Log);

    volatile int64 SideEffectGuard = 0;
    const FLoggingBenchmarkMetrics OnceMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        OnceAndThrottleIterations,
        [&SideEffectGuard](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                GAME_CLOG_ONCE(Index >= 0, Verbose, TEXT("Perf once shortcut %d"), Index);
                SideEffectGuard = SideEffectGuard + (Index & 1);
            }
        }
    );

    const FLoggingBenchmarkMetrics ThrottleSuppressedMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        OnceAndThrottleIterations,
        [](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                GAME_LOG_THROTTLE(1.0, Verbose, TEXT("Perf suppressed throttle %d"), Index);
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("GAME_CLOG_ONCE 触发后短路耗时 %.2f ns/op 应低于预算 %.0f ns/op"), OnceMetrics.MedianNs, OnceShortcutBudgetNsPerOp),
        OnceMetrics.MedianNs < OnceShortcutBudgetNsPerOp
    );
    TestTrue(
        FString::Printf(
            TEXT("GAME_LOG_THROTTLE 抑制前置短路耗时 %.2f ns/op 应低于预算 %.0f ns/op"),
            ThrottleSuppressedMetrics.MedianNs,
            ThrottleSuppressedBudgetNsPerOp
        ),
        ThrottleSuppressedMetrics.MedianNs < ThrottleSuppressedBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("[GameLoggingPerf] GAME_CLOG_ONCE Shortcut: %.2f ns/op (CV: %.2f%%, %.2f Mops/s, Guard=%lld) | GAME_LOG_THROTTLE (Suppressed): %.2f ns/op (CV: %.2f%%, %.2f Mops/s)"),
        OnceMetrics.MedianNs,
        OnceMetrics.CvPercent,
        OnceMetrics.OpsPerSec / 1.0e6,
        SideEffectGuard,
        ThrottleSuppressedMetrics.MedianNs,
        ThrottleSuppressedMetrics.CvPercent,
        ThrottleSuppressedMetrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingRegistryScalingPerfTest,
    "Game.Logging.Perf.RegistryLookupScaling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameLogRegistry 哈希索引在 200 模块规模下的 O(1) 查询吞吐微基准
 * @param Parameters 自动化测试参数（未使用）
 * @return 预算内返回 true
 */
bool FGameLoggingRegistryScalingPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 ScaleCount = 200;
    TArray<FString> ModuleNames;
    ModuleNames.Reserve(ScaleCount);
    for (int32 Index = 0; Index < ScaleCount; ++Index) {
        ModuleNames.Add(FString::Printf(TEXT("Perf_RegistryMod_%03d"), Index));
    }

    const auto Cleanup = MakeCancelableScopeGuard([&ModuleNames]() {
        for (const FString& Name : ModuleNames) {
            FTCHARToUTF8 AnsiName(*Name);
            Game::Log::FGameLogRegistry::UnregisterCategory(AnsiName.Get());
        }
    });

    for (const FString& Name : ModuleNames) {
        FTCHARToUTF8 AnsiName(*Name);
        Game::Log::FGameLogRegistry::RegisterCategory(AnsiName.Get(), &LogPerfDynamicTarget);
    }

    const FTCHARToUTF8 TargetAnsi(*ModuleNames[ScaleCount - 1]);
    const char* TargetName = TargetAnsi.Get();
    volatile uintptr_t Sink = 0;

    const FLoggingBenchmarkMetrics LookupMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RegistryLookupIterations,
        [TargetName, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FLogCategoryBase& Cat = Game::Log::FGameLogRegistry::GetCategory(TargetName);
                Sink = Sink ^ reinterpret_cast<uintptr_t>(&Cat);
            }
        }
    );

    const FLoggingBenchmarkMetrics SlotResolveMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RegistryLookupIterations,
        [TargetName, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const Game::Log::FLogCategorySlot* Slot = Game::Log::FGameLogRegistry::ResolveCategorySlot(TargetName);
                Sink = Sink ^ reinterpret_cast<uintptr_t>(Slot);
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("200 模块规模 GetCategory 耗时 %.2f ns/op 应低于预算 %.0f ns/op"), LookupMetrics.MedianNs, RegistryLookup200BudgetNsPerOp),
        LookupMetrics.MedianNs < RegistryLookup200BudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("[GameLoggingPerf] Registry O(1) Lookup (200 modules): GetCategory=%.2f ns/op (CV: %.2f%%, %.2f Mops/s) | ResolveCategorySlot=%.2f ns/op (Sink=%llu)"),
        LookupMetrics.MedianNs,
        LookupMetrics.CvPercent,
        LookupMetrics.OpsPerSec / 1.0e6,
        SlotResolveMetrics.MedianNs,
        static_cast<unsigned long long>(Sink)
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingRemoteDevicePerfTest,
    "Game.Logging.Perf.RemoteDeviceFilterAndRingQueue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameRemoteLogOutputDevice 级别过滤拦截、黑名单过滤与无锁环形队列入队/排出微基准
 * @param Parameters 自动化测试参数（未使用）
 * @return 预算内返回 true
 */
bool FGameLoggingRemoteDevicePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRemoteLogOutputDevice Device;
    Device.SetMinVerbosity(ELogVerbosity::Warning);
    const FName AllowedCat(TEXT("LogPerfAllowed"));
    const FName IgnoredCat(TEXT("LogGameMessage"));

    const FLoggingBenchmarkMetrics VerbosityFilteredMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RemoteFilterIterations,
        [&Device, AllowedCat](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Device.Serialize(TEXT("FilteredInfoLog"), ELogVerbosity::Log, AllowedCat);
            }
        }
    );

    const FLoggingBenchmarkMetrics BlacklistFilteredMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RemoteFilterIterations / 4,
        [&Device, IgnoredCat](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Device.Serialize(TEXT("IgnoredWarningLog"), ELogVerbosity::Warning, IgnoredCat, 1.0);
            }
        }
    );

    TArray<FGameRemoteLogEntry> DrainBuffer;
    DrainBuffer.Reserve(RemoteRingBatchSize);
    const FLoggingBenchmarkMetrics RingRoundTripMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RemoteRingBatchSize * RemoteRingBatchesPerRound,
        [&Device, &DrainBuffer, AllowedCat]([[maybe_unused]] const int32 TotalOps) {
            for (int32 Batch = 0; Batch < RemoteRingBatchesPerRound; ++Batch) {
                for (int32 Index = 0; Index < RemoteRingBatchSize; ++Index) {
                    Device.Serialize(TEXT("RingBufferPayloadMessage"), ELogVerbosity::Warning, AllowedCat, 42.0);
                }
                DrainBuffer.Reset();
                (void)Device.DrainBatch(DrainBuffer, RemoteRingBatchSize);
            }
        }
    );

    TestTrue(
        FString::Printf(
            TEXT("级别过滤热路径耗时 %.2f ns/op 应低于预算 %.0f ns/op"),
            VerbosityFilteredMetrics.MedianNs,
            RemoteFilterBudgetNsPerOp
        ),
        VerbosityFilteredMetrics.MedianNs < RemoteFilterBudgetNsPerOp
    );
    TestTrue(
        FString::Printf(
            TEXT("环形队列入队+排出耗时 %.2f ns/op 应低于预算 %.0f ns/op"),
            RingRoundTripMetrics.MedianNs,
            RemoteRingRoundTripBudgetNsPerOp
        ),
        RingRoundTripMetrics.MedianNs < RemoteRingRoundTripBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("[GameLoggingPerf] RemoteDevice: VerbosityReject=%.2f ns/op (CV: %.2f%%) | BlacklistReject=%.2f ns/op | Ring Enqueue+Drain=%.2f ns/op (CV: %.2f%%, %.2f Mops/s)"),
        VerbosityFilteredMetrics.MedianNs,
        VerbosityFilteredMetrics.CvPercent,
        BlacklistFilteredMetrics.MedianNs,
        RingRoundTripMetrics.MedianNs,
        RingRoundTripMetrics.CvPercent,
        RingRoundTripMetrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingRemoteDeviceMultiThreadPerfTest,
    "Game.Logging.Perf.RemoteDeviceMultiThreadThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 8 线程并发写入 TLockFreeRingBuffer 与主线程排空吞吐微基准
 * @param Parameters 自动化测试参数（未使用）
 * @return 预算内返回 true
 */
bool FGameLoggingRemoteDeviceMultiThreadPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRemoteLogOutputDevice Device;
    Device.SetMinVerbosity(ELogVerbosity::Warning);
    const FName AllowedCat(TEXT("LogPerfMultiThread"));

    constexpr int32 WorkerCount = 8;
    const int32 LogsPerWorker = RemoteMultiThreadBatchLogs / WorkerCount;
    TArray<FGameRemoteLogEntry> DrainBuffer;
    DrainBuffer.Reserve(RemoteMultiThreadBatchLogs);

    const FLoggingBenchmarkMetrics MtMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RemoteMultiThreadBatchLogs * RemoteMultiThreadBatchesPerRound,
        [&Device, &DrainBuffer, AllowedCat, WorkerCount, LogsPerWorker]([[maybe_unused]] const int32 TotalLogs) {
            for (int32 Batch = 0; Batch < RemoteMultiThreadBatchesPerRound; ++Batch) {
                ParallelFor(WorkerCount, [&Device, AllowedCat, LogsPerWorker](int32) {
                    for (int32 Step = 0; Step < LogsPerWorker; ++Step) {
                        Device.Serialize(TEXT("MTLogPayload"), ELogVerbosity::Warning, AllowedCat, 10.0);
                    }
                });
                DrainBuffer.Reset();
                (void)Device.DrainBatch(DrainBuffer, RemoteMultiThreadBatchLogs);
            }
        }
    );

    TestEqual(TEXT("压测收尾后队列应清空"), Device.GetQueuedCount(), 0);
    TestTrue(
        FString::Printf(TEXT("8 线程并发入队+排空耗时 %.2f ns/op 应低于预算 %.0f ns/op"), MtMetrics.MedianNs, RemoteMultiThreadBudgetNsPerOp),
        MtMetrics.MedianNs < RemoteMultiThreadBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("[GameLoggingPerf] RemoteDevice 8-Thread Enqueue+Drain (%d logs/round): %.2f ns/op (CV: %.2f%%, %.2f Mops/s)"),
        RemoteMultiThreadBatchLogs * RemoteMultiThreadBatchesPerRound,
        MtMetrics.MedianNs,
        MtMetrics.CvPercent,
        MtMetrics.OpsPerSec / 1.0e6
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoggingSubsystemPerfTest,
    "Game.Logging.Perf.SubsystemEndToEndBroadcast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 对比 UGameLoggingSubsystem 无监听者零分配丢弃快路径与活跃监听者端到端广播耗时微基准
 * @param Parameters 自动化测试参数（未使用）
 * @return 预算内返回 true
 */
bool FGameLoggingSubsystemPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameLoggingPerfWorld")));
    const auto ShutdownGuard = MakeCancelableScopeGuard([GameInstance]() {
        GameInstance->Shutdown();
        GameInstance->MarkAsGarbage();
    });

    UGameLoggingSubsystem* LoggingSubsystem = UGameLoggingSubsystem::GetPtr(GameInstance);
    UGameplayMessageSubsystem* MessageBus = UGameplayMessageSubsystem::GetPtr(GameInstance);
    TestNotNull(TEXT("UGameLoggingSubsystem 有效"), LoggingSubsystem);
    TestNotNull(TEXT("UGameplayMessageSubsystem 有效"), MessageBus);
    if (LoggingSubsystem == nullptr || MessageBus == nullptr) {
        return false;
    }

    LoggingSubsystem->SetMinVerbosity(ELogVerbosity::Display);
    LoggingSubsystem->SetMaxDrainBatchPerTick(SubsystemBatchLogs);
    LoggingSubsystem->ClearQueue();

    const FName PerfCat(TEXT("LogSubsystemPerf"));

    const FLoggingBenchmarkMetrics NoListenerMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SubsystemBatchLogs,
        [LoggingSubsystem, PerfCat](const int32 BatchCount) {
            for (int32 Index = 0; Index < BatchCount; ++Index) {
                GLog->Serialize(TEXT("SubsystemPerfMsg"), ELogVerbosity::Display, PerfCat, 1.0);
            }
            (void)LoggingSubsystem->FlushQueue();
        }
    );

    volatile int64 ReceivedSink = 0;
    FGameplayMessageListenerHandle Handle = MessageBus->RegisterListener<FGameRemoteLogEntry>(
        TAG_Game_Log_Remote,
        [&ReceivedSink](const FGameRemoteLogEntry& Entry) {
            ReceivedSink = ReceivedSink + Entry.FrameNumber + 1;
        }
    );

    const FLoggingBenchmarkMetrics BroadcastMetrics = RunLoggingBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SubsystemBatchLogs,
        [LoggingSubsystem, PerfCat](const int32 BatchCount) {
            for (int32 Index = 0; Index < BatchCount; ++Index) {
                GLog->Serialize(TEXT("SubsystemPerfMsg"), ELogVerbosity::Display, PerfCat, 1.0);
            }
            (void)LoggingSubsystem->FlushQueue();
        }
    );

    Handle.Unregister();

    TestTrue(
        FString::Printf(
            TEXT("子系统端到端广播耗时 %.2f ns/op 应低于预算 %.0f ns/op"),
            BroadcastMetrics.MedianNs,
            SubsystemBroadcastBudgetNsPerOp
        ),
        BroadcastMetrics.MedianNs < SubsystemBroadcastBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("[GameLoggingPerf] Subsystem End-to-End (%d/batch): NoListenerDiscard=%.2f ns/op (CV: %.2f%%) | ActiveBroadcast=%.2f ns/op (CV: %.2f%%, %.2f Mops/s, Sink=%lld)"),
        SubsystemBatchLogs,
        NoListenerMetrics.MedianNs,
        NoListenerMetrics.CvPercent,
        BroadcastMetrics.MedianNs,
        BroadcastMetrics.CvPercent,
        BroadcastMetrics.OpsPerSec / 1.0e6,
        ReceivedSink
    ));

    return true;
}

#endif // WITH_AUTOMATION_TESTS
