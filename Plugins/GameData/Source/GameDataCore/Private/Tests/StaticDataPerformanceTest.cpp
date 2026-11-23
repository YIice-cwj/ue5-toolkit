// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file StaticDataPerformanceTest.cpp
 * @brief 静态数据核心组件全系列微基准测试（编解码、摘要、JSON 500 行加载、仓库往返、RCU 快照与目录指纹）
 */

#include "StaticData/FStaticDataBinCodec.h"
#include "StaticData/FStaticDataRepository.h"
#include "StaticData/TStaticDataSnapshotStore.h"
#include "Tests/FStaticDataRepositoryTestRows.h"
#include "Tests/GameDataBenchmarkUtils.h"
#include "Utils/DataTableLoaderUtils.h"
#include "Utils/FDirectoryChangeWatcher.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace StaticDataPerf {

/**
 * @brief 基准测试行 A 的生成式免反射读写器
 * @param Ar 归档对象
 * @param RowPtr 行指针
 * @return 归档无错返回 true
 */
bool PerfHandleTestRowA(FArchive& Ar, void* RowPtr) {
    FRepositoryTestRowA& Row = *static_cast<FRepositoryTestRowA*>(RowPtr);
    Ar << Row.Score;
    Ar << Row.Weight;
    return !Ar.IsError();
}

/**
 * @brief 性能基准测试专用快照载体结构体
 */
struct FPerfSnapshot {
    uint32 DataVersion = 1;
    int32 Payload = 42;
};

}  // namespace StaticDataPerf

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataBinCodecPerfTest,
    "Game.Data.Perf.StaticData.BinCodec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：.bin 格式编解码吞吐（1000 行表，单缓冲直写 + 全包 CRC32）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataBinCodecPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    TMap<FName, FStaticDataBinaryTable> Tables;
    FStaticDataBinaryTable& Table = Tables.Add(FName(TEXT("FRepositoryTestRowA")));
    Table.StructName = TEXT("FRepositoryTestRowA");

    constexpr int32 RowCount = 1000;
    Table.Rows.Reserve(RowCount);
    for (int32 Index = 0; Index < RowCount; ++Index) {
        const FName RowKey(*FString::Printf(TEXT("Row_%04d"), Index));
        TArray<uint8>& Payload = Table.Rows.Add(RowKey);
        Payload.SetNumUninitialized(8);
        FMemory::Memcpy(Payload.GetData(), &Index, sizeof(int32));
        const float Weight = static_cast<float>(Index) * 0.5f;
        FMemory::Memcpy(Payload.GetData() + 4, &Weight, sizeof(float));
    }

    auto PreEncodeRes = FStaticDataBinCodec::Encode(Tables);
    TestTrue(TEXT("预编码成功"), PreEncodeRes.IsSuccess());
    const TArray<uint8> EncodedBytes = PreEncodeRes.IsSuccess() ? PreEncodeRes.ExtractValue() : TArray<uint8>();

    constexpr int32 EncodeIterations = 500;
    const FBenchmarkMetrics EncodeMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, EncodeIterations,
        [&Tables](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = FStaticDataBinCodec::Encode(Tables);
                check(Res.IsSuccess());
            }
        }
    );

    constexpr int32 DecodeIterations = 1000;
    const FBenchmarkMetrics DecodeMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, DecodeIterations,
        [&EncodedBytes](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = FStaticDataBinCodec::Decode(EncodedBytes);
                check(Res.IsSuccess());
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("FStaticDataBinCodec (1000 rows): Encode=%0.2f us/op (%0.1f kops/s, CV=%0.1f%%) | Decode=%0.2f us/op (%0.1f kops/s, CV=%0.1f%%)"),
        EncodeMetrics.MedianNs / 1000.0, EncodeMetrics.OpsPerSec / 1000.0, EncodeMetrics.CvPercent,
        DecodeMetrics.MedianNs / 1000.0, DecodeMetrics.OpsPerSec / 1000.0, DecodeMetrics.CvPercent
    ));

    TestTrue(TEXT("编码性能满足预算 (< 1 ms / 1000 行)"), EncodeMetrics.MedianNs < 1.0e6);
    TestTrue(TEXT("解码性能满足预算 (< 500 us / 1000 行)"), DecodeMetrics.MedianNs < 500.0e3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataDigestPerfTest,
    "Game.Data.Perf.StaticData.ContentDigest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：全表排序带长度前缀 SHA-1 内容摘要计算吞吐（1000 行）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataDigestPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    FStaticDataRepository Repository;
    Repository.RegisterRowSerializer<FRepositoryTestRowA>(&StaticDataPerf::PerfHandleTestRowA);

    TMap<FName, FRepositoryTestRowA> Rows;
    constexpr int32 RowCount = 1000;
    Rows.Reserve(RowCount);
    for (int32 Index = RowCount - 1; Index >= 0; --Index) {
        const FName Key(*FString::Printf(TEXT("Item_%04d"), Index));
        Rows.Add(Key, FRepositoryTestRowA{Index, static_cast<float>(Index) * 1.5f});
    }
    TestTrue(TEXT("写入摘要基准测试表成功"), Repository.AddTable(Rows).IsSuccess());

    constexpr int32 DigestIterations = 500;
    const FBenchmarkMetrics DigestMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, DigestIterations,
        [&Repository](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                const FStaticDataContentDigest Digest = Repository.ComputeContentDigest();
                check(Digest.TableCount == 1);
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("ComputeContentDigest (1000 rows): %0.2f us/op (%0.1f kops/s, CV=%0.1f%%)"),
        DigestMetrics.MedianNs / 1000.0, DigestMetrics.OpsPerSec / 1000.0, DigestMetrics.CvPercent
    ));

    TestTrue(TEXT("内容摘要计算满足预算 (< 500 us)"), DigestMetrics.MedianNs < 500.0e3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataJsonLoadPerfTest,
    "Game.Data.Perf.StaticData.JsonLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：FDataTableLoaderUtils 真实规模 JSON（500 行）加载解析吞吐
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataJsonLoadPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    const FString JsonFilePath = FPaths::ProjectSavedDir() / TEXT("UnitTest_Perf_DT_500.json");
    constexpr int32 RowCount = 500;
    FString JsonContent;
    JsonContent.Reserve(RowCount * 64);
    JsonContent.Append(TEXT("["));
    for (int32 Index = 0; Index < RowCount; ++Index) {
        if (Index > 0) {
            JsonContent.Append(TEXT(","));
        }
        JsonContent.Appendf(TEXT("{\"RowName\":\"Item_%04d\",\"Score\":%d,\"Weight\":%.2f}"),
            Index, Index * 10, static_cast<float>(Index) * 0.5f);
    }
    JsonContent.Append(TEXT("]"));
    TestTrue(TEXT("写入 500 行基准 JSON 成功"), FFileHelper::SaveStringToFile(JsonContent, *JsonFilePath));

    constexpr int32 JsonIterations = 25;
    const FBenchmarkMetrics JsonMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, JsonIterations,
        [&JsonFilePath](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(JsonFilePath);
                check(Res.IsSuccess() && Res.GetValue().Num() == RowCount);
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("LoadTableFromJson (500 rows): %0.2f ms/op (%0.1f ops/s, CV=%0.1f%%)"),
        JsonMetrics.MedianNs / 1.0e6, JsonMetrics.OpsPerSec, JsonMetrics.CvPercent
    ));

    TestTrue(TEXT("500 行 JSON 加载满足预算 (< 25 ms)"), JsonMetrics.MedianNs < 25.0e6);
    IFileManager::Get().Delete(*JsonFilePath);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataRepositoryPerfTest,
    "Game.Data.Perf.StaticData.RepositoryRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：仓库 AddTable / ExtractTable 与 .bin ExportToFile / ImportFromFile 吞吐（1000 行）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataRepositoryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    constexpr int32 RowCount = 1000;
    TMap<FName, FRepositoryTestRowA> Rows;
    Rows.Reserve(RowCount);
    for (int32 Index = 0; Index < RowCount; ++Index) {
        Rows.Add(FName(*FString::Printf(TEXT("Row_%04d"), Index)), FRepositoryTestRowA{Index, static_cast<float>(Index) * 0.25f});
    }

    FStaticDataRepository Repo;
    Repo.RegisterRowSerializer<FRepositoryTestRowA>(&StaticDataPerf::PerfHandleTestRowA);

    constexpr int32 MemIterations = 200;
    const FBenchmarkMetrics AddExtractMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, MemIterations,
        [&Repo, &Rows](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                check(Repo.AddTable(Rows).IsSuccess());
                auto ExtractRes = Repo.ExtractTable<FRepositoryTestRowA>();
                check(ExtractRes.IsSuccess() && ExtractRes.GetValue().Num() == RowCount);
            }
        }
    );

    const FString BinPath = FPaths::ProjectSavedDir() / TEXT("UnitTest_Perf_Repo.bin");
    TestTrue(TEXT("预置仓库数据成功"), Repo.AddTable(Rows).IsSuccess());

    constexpr int32 FileIterations = 50;
    const FBenchmarkMetrics FileRoundTripMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, FileIterations,
        [&Repo, &BinPath](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                check(Repo.ExportToFile(BinPath).IsSuccess());
                FStaticDataRepository ReaderRepo;
                auto ImportRes = ReaderRepo.ImportFromFile(BinPath);
                check(ImportRes.IsSuccess() && ImportRes.GetValue() == 1);
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("Repository (1000 rows): Add+Extract=%0.2f us/op (%0.1f kops/s, CV=%0.1f%%) | Export+Import=%0.2f us/op (%0.1f kops/s, CV=%0.1f%%)"),
        AddExtractMetrics.MedianNs / 1000.0, AddExtractMetrics.OpsPerSec / 1000.0, AddExtractMetrics.CvPercent,
        FileRoundTripMetrics.MedianNs / 1000.0, FileRoundTripMetrics.OpsPerSec / 1000.0, FileRoundTripMetrics.CvPercent
    ));

    TestTrue(TEXT("仓库内存 Add+Extract 满足预算 (< 2 ms / 1000 行)"), AddExtractMetrics.MedianNs < 2.0e6);
    TestTrue(TEXT("仓库文件 Export+Import 满足预算 (< 5 ms / 1000 行)"), FileRoundTripMetrics.MedianNs < 5.0e6);
    IFileManager::Get().Delete(*BinPath);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataSnapshotStorePerfTest,
    "Game.Data.Perf.StaticData.SnapshotStore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：TStaticDataSnapshotStore 热点读取（Acquire 与无锁 GetVersion）吞吐
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataSnapshotStorePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    TStaticDataSnapshotStore<StaticDataPerf::FPerfSnapshot> Store;
    Store.Publish(MakeShared<StaticDataPerf::FPerfSnapshot>());

    constexpr int32 ReadIterations = 100000;
    const FBenchmarkMetrics AcquireMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, ReadIterations,
        [&Store](const int32 Iters) {
            uint32 Acc = 0;
            for (int32 i = 0; i < Iters; ++i) {
                const TSharedPtr<const StaticDataPerf::FPerfSnapshot> Snap = Store.Acquire();
                Acc += Snap->DataVersion;
            }
            check(Acc == static_cast<uint32>(Iters));
        }
    );

    const FBenchmarkMetrics VersionMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, ReadIterations,
        [&Store](const int32 Iters) {
            uint32 Acc = 0;
            for (int32 i = 0; i < Iters; ++i) {
                Acc += Store.GetVersion();
            }
            check(Acc == static_cast<uint32>(Iters));
        }
    );

    AddInfo(FString::Printf(
        TEXT("TStaticDataSnapshotStore: Acquire=%0.1f ns/op (%0.2f Mops/s, CV=%0.1f%%) | GetVersion(lock-free)=%0.1f ns/op (%0.2f Mops/s, CV=%0.1f%%)"),
        AcquireMetrics.MedianNs, AcquireMetrics.OpsPerSec / 1.0e6, AcquireMetrics.CvPercent,
        VersionMetrics.MedianNs, VersionMetrics.OpsPerSec / 1.0e6, VersionMetrics.CvPercent
    ));

    TestTrue(TEXT("Acquire 满足高频读预算 (< 500 ns)"), AcquireMetrics.MedianNs < 500.0);
    TestTrue(TEXT("无锁 GetVersion 满足预算 (< 100 ns)"), VersionMetrics.MedianNs < 100.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDirectoryFingerprintPerfTest,
    "Game.Data.Perf.StaticData.DirectoryWatcherFingerprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：FDirectoryChangeWatcher::ComputeDirectoryFingerprint 32 文件目录扫描吞吐
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FDirectoryFingerprintPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    const FString PerfDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UnitTest_Perf_WatcherDir"));
    IFileManager::Get().DeleteDirectory(*PerfDir, false, true);
    TestTrue(TEXT("创建目录指纹基准测试目录成功"), IFileManager::Get().MakeDirectory(*PerfDir, true));

    constexpr int32 FileCount = 32;
    for (int32 Index = 0; Index < FileCount; ++Index) {
        const FString FilePath = PerfDir / FString::Printf(TEXT("Table_%02d.json"), Index);
        FFileHelper::SaveStringToFile(TEXT("[{\"RowName\":\"K1\"}]"), *FilePath);
    }

    constexpr int32 ScanIterations = 100;
    const FBenchmarkMetrics ScanMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, ScanIterations,
        [&PerfDir](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                const FDirectoryFingerprint Fp = FDirectoryChangeWatcher::ComputeDirectoryFingerprint(PerfDir, TEXT("*.json"), true);
                check(Fp.FileCount == FileCount);
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("DirectoryWatcher Fingerprint (32 files): %0.2f us/op (%0.1f kops/s, CV=%0.1f%%)"),
        ScanMetrics.MedianNs / 1000.0, ScanMetrics.OpsPerSec / 1000.0, ScanMetrics.CvPercent
    ));

    TestTrue(TEXT("32 文件目录指纹扫描满足预算 (< 5 ms)"), ScanMetrics.MedianNs < 5.0e6);
    IFileManager::Get().DeleteDirectory(*PerfDir, false, true);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
