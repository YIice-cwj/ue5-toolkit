// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file SaveGamePerformanceTest.cpp
 * @brief 存档系统核心组件全系列微基准测试（段构建/恢复、4KB/64KB 原子文件 I/O、子系统端到端往返）
 */

#include "Save/FSaveGameFileIO.h"
#include "Save/FSaveGameSegmentRegistry.h"
#include "Save/USaveGameSubsystem.h"
#include "Tests/GameDataBenchmarkUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSegmentRegistryPerfTest,
    "Game.Data.Perf.SaveGame.SegmentRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：存档段结构构建与恢复吞吐（16 段混合数据）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FSaveGameSegmentRegistryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
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
        TestTrue(TEXT("注册基准存档段成功"), Registry.RegisterSegment(Seg).IsSuccess());
    }

    const FString SlotName = TEXT("Perf_Slot");
    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>();

    constexpr int32 BuildIterations = 1000;
    const FBenchmarkMetrics BuildMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, BuildIterations,
        [&Registry, &SlotName, Archive](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = Registry.BuildArchive(SlotName, *Archive);
                check(Res.IsSuccess());
            }
        }
    );

    constexpr int32 ApplyIterations = 1000;
    const FBenchmarkMetrics LoadMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, ApplyIterations,
        [&Registry, &SlotName, Archive](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = Registry.ApplyArchive(SlotName, *Archive);
                check(Res.IsSuccess());
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("SegmentRegistry (16 segments): BuildArchive=%0.2f us/op (%0.1f kops/s, CV=%0.1f%%) | ApplyArchive=%0.2f us/op (%0.1f kops/s, CV=%0.1f%%)"),
        BuildMetrics.MedianNs / 1000.0, BuildMetrics.OpsPerSec / 1000.0, BuildMetrics.CvPercent,
        LoadMetrics.MedianNs / 1000.0, LoadMetrics.OpsPerSec / 1000.0, LoadMetrics.CvPercent
    ));

    TestTrue(TEXT("段构建性能满足预算 (< 150 us / 16 段)"), BuildMetrics.MedianNs < 150.0e3);
    TestTrue(TEXT("段恢复性能满足预算 (< 100 us / 16 段)"), LoadMetrics.MedianNs < 100.0e3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameFileIOPerfTest,
    "Game.Data.Perf.SaveGame.AtomicFileIO",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：原子写盘与读盘吞吐（4KB 与 64KB 存档块）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FSaveGameFileIOPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    const FString SlotName = TEXT("Perf_AtomicSave_TestSlot");
    static constexpr int32 BenchSaveGameFileTypeTag = 0x53415647;

    auto MakeFakePayload = [](int32 SizeBytes) {
        TArray<uint8> Bytes;
        Bytes.SetNumUninitialized(SizeBytes);
        FMemory::Memset(Bytes.GetData(), 0xAB, Bytes.Num());
        FMemory::Memcpy(Bytes.GetData(), &BenchSaveGameFileTypeTag, sizeof(int32));
        return Bytes;
    };

    const TArray<uint8> Payload4KB = MakeFakePayload(4096);
    const TArray<uint8> Payload64KB = MakeFakePayload(65536);

    const FBenchmarkMetrics Write4K = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, 80,
        [&SlotName, &Payload4KB](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                check(FSaveGameFileIO::WriteSlotBytes(SlotName, Payload4KB, 0).IsSuccess());
            }
        }
    );

    const FBenchmarkMetrics Read4K = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, 200,
        [&SlotName](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto ReadRes = FSaveGameFileIO::ReadSlotBytes(SlotName);
                check(ReadRes.IsSuccess());
            }
        }
    );

    const FBenchmarkMetrics Write64K = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, 40,
        [&SlotName, &Payload64KB](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                check(FSaveGameFileIO::WriteSlotBytes(SlotName, Payload64KB, 0).IsSuccess());
            }
        }
    );

    const FBenchmarkMetrics Read64K = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, 100,
        [&SlotName](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto ReadRes = FSaveGameFileIO::ReadSlotBytes(SlotName);
                check(ReadRes.IsSuccess());
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("Atomic File I/O 4KB: Write=%0.2f ms/op (%0.1f ops/s) | Read=%0.2f us/op (%0.1f kops/s)"),
        Write4K.MedianNs / 1.0e6, Write4K.OpsPerSec, Read4K.MedianNs / 1000.0, Read4K.OpsPerSec / 1000.0
    ));
    AddInfo(FString::Printf(
        TEXT("Atomic File I/O 64KB: Write=%0.2f ms/op (%0.1f ops/s) | Read=%0.2f us/op (%0.1f kops/s)"),
        Write64K.MedianNs / 1.0e6, Write64K.OpsPerSec, Read64K.MedianNs / 1000.0, Read64K.OpsPerSec / 1000.0
    ));

    TestTrue(TEXT("4KB 原子写盘满足预算 (< 10 ms)"), Write4K.MedianNs < 10.0e6);
    TestTrue(TEXT("4KB 读盘满足预算 (< 1 ms)"), Read4K.MedianNs < 1.0e6);
    TestTrue(TEXT("64KB 原子写盘满足预算 (< 20 ms)"), Write64K.MedianNs < 20.0e6);
    TestTrue(TEXT("64KB 读盘满足预算 (< 2 ms)"), Read64K.MedianNs < 2.0e6);

    (void)FSaveGameFileIO::DeleteSaveSlot(SlotName);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSubsystemRoundTripPerfTest,
    "Game.Data.Perf.SaveGame.SubsystemRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：USaveGameSubsystem 同步全链路 SaveGameToSlot + LoadGameFromSlot 吞吐
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FSaveGameSubsystemRoundTripPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("SaveGamePerfWorld")));
    USaveGameSubsystem* Subsystem = GameInstance->GetSubsystem<USaveGameSubsystem>();
    if (!TestNotNull(TEXT("基准测试存档子系统有效"), Subsystem)) {
        GameInstance->Shutdown();
        return false;
    }

    int32 Score = 42;
    FString ProfileName = TEXT("BenchmarkPlayer");
    FGameSaveSegment Seg;
    Seg.SegmentName = FName(TEXT("PerfProfile"));
    Seg.SegmentVersion = 1;
    Seg.Save = [&Score, &ProfileName](FArchive& Ar) {
        Ar << Score << ProfileName;
    };
    Seg.Load = [&Score, &ProfileName](FArchive& Ar, [[maybe_unused]] int32 Ver) {
        Ar << Score << ProfileName;
        return !Ar.IsError();
    };
    TestTrue(TEXT("注册端到端基准段成功"), Subsystem->RegisterSegment(Seg).IsSuccess());

    const FString SlotName = TEXT("Perf_SubsystemRoundTrip");
    const FBenchmarkMetrics RoundTripMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, 40,
        [Subsystem, &SlotName](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                check(Subsystem->SaveGameToSlot(SlotName).IsSuccess());
                check(Subsystem->LoadGameFromSlot(SlotName).IsSuccess());
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("USaveGameSubsystem Sync RoundTrip (Save+Load): %0.2f ms/op (%0.1f ops/s, CV=%0.1f%%)"),
        RoundTripMetrics.MedianNs / 1.0e6, RoundTripMetrics.OpsPerSec, RoundTripMetrics.CvPercent
    ));

    TestTrue(TEXT("子系统同步存读往返满足预算 (< 15 ms)"), RoundTripMetrics.MedianNs < 15.0e6);
    (void)Subsystem->DeleteSaveSlot(SlotName);
    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
