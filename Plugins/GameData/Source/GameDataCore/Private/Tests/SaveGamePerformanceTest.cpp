// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file SaveGamePerformanceTest.cpp
 * @brief 存档系统核心组件（原子文件 I/O、段反序列化、同步存取）性能微基准测试
 */

#include "Save/FSaveGameFileIO.h"
#include "Save/FSaveGameSegmentRegistry.h"
#include "Save/USaveGameSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮基准测试，消除预热偏差并统计中位数与离散度 (CV%)
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热轮数（测量后丢弃）
 * @param SampleRounds 正式采样轮数（规范要求 >= 5 轮）
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
    Metrics.MaxNs = SamplesNs[SampleRounds - 1];
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSegmentRegistryPerfTest,
    "Game.Data.Perf.SaveGame.SegmentRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：存档段结构装载与恢复吞吐（16 段混合数据）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FSaveGameSegmentRegistryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FSaveGameSegmentRegistry Registry;
    constexpr int32 SegmentCount = 16;
    for (int32 i = 0; i < SegmentCount; ++i) {
        FGameSaveSegment Seg;
        Seg.SegmentName = *FString::Printf(TEXT("PerfSeg_%d"), i);
        Seg.SegmentVersion = 1;
        Seg.Save = [i](FArchive& Ar) {
            int32 Val = i * 10;
            Ar << Val;
            FString Str = TEXT("PayloadDataString");
            Ar << Str;
        };
        Seg.Load = [](FArchive& Ar, [[maybe_unused]] int32 Version) -> bool {
            int32 Val = 0;
            Ar << Val;
            FString Str;
            Ar << Str;
            return !Ar.IsError();
        };
        Registry.RegisterSegment(Seg);
    }

    const FString SlotName = TEXT("Perf_Slot");
    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>();
    auto PreBuildRes = Registry.BuildArchive(SlotName, *Archive);
    TestTrue(TEXT("预构建归档成功"), PreBuildRes.IsSuccess());

    constexpr int32 RegistryIterations = 1000;
    const FBenchmarkMetrics LoadMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        RegistryIterations,
        [&Registry, &SlotName, Archive](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = Registry.ApplyArchive(SlotName, *Archive);
                check(Res.IsSuccess());
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("SegmentRegistry (16 segments): ApplyArchive=%0.2f us/op (%0.1f kops/s, CV=%0.1f%%)"),
        LoadMetrics.MedianNs / 1000.0, LoadMetrics.OpsPerSec / 1000.0, LoadMetrics.CvPercent
    ));

    TestTrue(TEXT("段恢复性能满足预算 (< 100 us / 16 段)"), LoadMetrics.MedianNs < 100.0e3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameFileIOPerfTest,
    "Game.Data.Perf.SaveGame.AtomicFileIO",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：原子写盘与读盘吞吐（4KB 存档块）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FSaveGameFileIOPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString SlotName = TEXT("Perf_AtomicSave_TestSlot");
    TArray<uint8> FakeSaveBytes;
    FakeSaveBytes.SetNumUninitialized(4096);
    FMemory::Memset(FakeSaveBytes.GetData(), 0xAB, FakeSaveBytes.Num());
    constexpr int32 SaveGameFileTypeTag = 0x53415647;
    FMemory::Memcpy(FakeSaveBytes.GetData(), &SaveGameFileTypeTag, sizeof(int32));

    constexpr int32 WriteIterations = 100;
    const FBenchmarkMetrics WriteMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        WriteIterations,
        [&SlotName, &FakeSaveBytes](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = FSaveGameFileIO::WriteSlotBytes(SlotName, FakeSaveBytes, 0);
                check(Res.IsSuccess());
            }
        }
    );

    constexpr int32 ReadIterations = 200;
    const FBenchmarkMetrics ReadMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ReadIterations,
        [&SlotName](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto ReadRes = FSaveGameFileIO::ReadSlotBytes(SlotName);
                check(ReadRes.IsSuccess());
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("Atomic File I/O (4KB payload): Write=%0.2f ms/op (%0.1f ops/s) | Read=%0.2f us/op (%0.1f kops/s)"),
        WriteMetrics.MedianNs / 1.0e6, WriteMetrics.OpsPerSec,
        ReadMetrics.MedianNs / 1000.0, ReadMetrics.OpsPerSec / 1000.0
    ));

    TestTrue(TEXT("原子写盘满足预算 (< 10 ms)"), WriteMetrics.MedianNs < 10.0e6);
    TestTrue(TEXT("读盘满足预算 (< 1 ms)"), ReadMetrics.MedianNs < 1.0e6);

    FSaveGameFileIO::DeleteSaveSlot(SlotName);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
