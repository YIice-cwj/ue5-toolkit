// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file StaticDataPerformanceTest.cpp
 * @brief 静态数据核心组件（二进制编解码、内容摘要、JSON 反序列化）性能微基准测试
 */

#include "StaticData/FStaticDataBinCodec.h"
#include "StaticData/FStaticDataRepository.h"
#include "Utils/DataTableLoaderUtils.h"
#include "Tests/FStaticDataRepositoryTestRows.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace StaticDataPerf {

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

bool PerfHandleTestRowA(FArchive& Ar, void* RowPtr) {
    FRepositoryTestRowA& Row = *static_cast<FRepositoryTestRowA*>(RowPtr);
    Ar << Row.Score;
    Ar << Row.Weight;
    return !Ar.IsError();
}

}  // namespace StaticDataPerf

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataBinCodecPerfTest,
    "Game.Data.Perf.StaticData.BinCodec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：.bin 格式编解码吞吐（1000 行表，全包 CRC32）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataBinCodecPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace StaticDataPerf;
    TMap<FName, FStaticDataBinaryTable> Tables;
    FStaticDataBinaryTable& Table = Tables.Add(FName(TEXT("PerfRowA")));
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
    TArray<uint8> EncodedBytes = PreEncodeRes.IsSuccess() ? PreEncodeRes.ExtractValue() : TArray<uint8>();

    constexpr int32 EncodeIterations = 500;
    const FBenchmarkMetrics EncodeMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        EncodeIterations,
        [&Tables](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = FStaticDataBinCodec::Encode(Tables);
                check(Res.IsSuccess());
            }
        }
    );

    constexpr int32 DecodeIterations = 1000;
    const FBenchmarkMetrics DecodeMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        DecodeIterations,
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
 * @brief 微基准测试：全表排序内容摘要计算吞吐（1000 行）
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataDigestPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace StaticDataPerf;
    FStaticDataRepository Repository;
    Repository.RegisterRowSerializer(FName(FRepositoryTestRowA::StaticStruct()->GetName()), &PerfHandleTestRowA);

    TMap<FName, FRepositoryTestRowA> Rows;
    constexpr int32 RowCount = 1000;
    for (int32 Index = RowCount - 1; Index >= 0; --Index) {
        const FName Key(*FString::Printf(TEXT("Item_%04d"), Index));
        FRepositoryTestRowA Row;
        Row.Score = Index;
        Row.Weight = static_cast<float>(Index) * 1.5f;
        Rows.Add(Key, Row);
    }
    Repository.AddTable(Rows);

    constexpr int32 DigestIterations = 500;
    const FBenchmarkMetrics DigestMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        DigestIterations,
        [&Repository](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                FStaticDataContentDigest Digest = Repository.ComputeContentDigest();
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
 * @brief 微基准测试：FDataTableLoaderUtils JSON 表文件加载解析吞吐
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FStaticDataJsonLoadPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace StaticDataPerf;
    const FString JsonFilePath = FPaths::ProjectSavedDir() / TEXT("UnitTest_Perf_DT.json");
    const FString JsonContent = TEXT("[{\"RowName\":\"Item_1\",\"Score\":10,\"Weight\":1.5},{\"RowName\":\"Item_2\",\"Score\":20,\"Weight\":2.5}]");
    FFileHelper::SaveStringToFile(JsonContent, *JsonFilePath);

    constexpr int32 JsonIterations = 500;
    const FBenchmarkMetrics JsonMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        JsonIterations,
        [&JsonFilePath](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                auto Res = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(JsonFilePath);
                check(Res.IsSuccess());
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("LoadTableFromJson (2 rows): %0.2f us/op (%0.1f kops/s, CV=%0.1f%%)"),
        JsonMetrics.MedianNs / 1000.0, JsonMetrics.OpsPerSec / 1000.0, JsonMetrics.CvPercent
    ));

    TestTrue(TEXT("JSON 解析性能满足预算 (< 500 us / 2 行)"), JsonMetrics.MedianNs < 500.0e3);
    IFileManager::Get().Delete(*JsonFilePath);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
