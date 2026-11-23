// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameCloudSavePerformanceTest.cpp
 * @brief 云端存档同步子系统（上传、先验后落盘下载、元信息查询）性能微基准测试
 */

#include "Cloud/GameCloudSaveSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Async/FGameThreadTaskRunner.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Message/GameNetworkMessageTypes.h"
#include "Misc/AutomationTest.h"
#include "Save/USaveGameSubsystem.h"
#include "System/GameNetServiceSubsystem.h"
#include "Tests/GameDataBenchmarkUtils.h"
#include "Transport/MockNetTransport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCloudSavePerfTest,
    "Game.Data.Perf.CloudSave.Throughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准测试：基于 FMockNetTransport 的云存档 Upload / Download(先验后落盘+零二次读盘) / Query 吞吐
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FGameCloudSavePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameDataBenchmark;
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("CloudSavePerfWorld")));

    UGameCloudSaveSubsystem& CloudSave = UGameCloudSaveSubsystem::Get(GameInstance);
    USaveGameSubsystem* SaveSys = GameInstance->GetSubsystem<USaveGameSubsystem>();
    UGameNetServiceSubsystem& NetService = UGameNetServiceSubsystem::Get(GameInstance);
    if (!TestNotNull(TEXT("本地持久化子系统有效"), SaveSys)) {
        GameInstance->Shutdown();
        return false;
    }

    const TSharedRef<FMockNetTransport> MockTransport = MakeShared<FMockNetTransport>();
    NetService.SetTransport(MockTransport);
    NetService.Connect(TEXT("mock://cloud-save-perf"));

    TMap<FString, FGameCloudSaveUploadReq> CloudStore;
    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_UPLOAD_REQ, NET_MSG_CLOUD_SAVE_UPLOAD_RSP,
        [&CloudStore, &NetService]([[maybe_unused]] const FNetPacketHeader&, TConstArrayView<uint8> ReqBytes) -> TOptional<TArray<uint8>> {
            auto Req = NetService.GetSerializer()->DeserializeStruct<FGameCloudSaveUploadReq>(ReqBytes);
            if (Req.IsFailure()) {
                return NullOpt;
            }
            CloudStore.Add(Req.GetValue().SlotName, Req.GetValue());
            FGameCloudSaveUploadRsp Rsp;
            Rsp.bSuccess = true;
            Rsp.SlotName = Req.GetValue().SlotName;
            auto Enc = NetService.GetSerializer()->SerializeStruct(Rsp);
            return Enc.IsSuccess() ? Enc.GetValue() : TOptional<TArray<uint8>>{};
        }
    );
    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_DOWNLOAD_REQ, NET_MSG_CLOUD_SAVE_DOWNLOAD_RSP,
        [&CloudStore, &NetService]([[maybe_unused]] const FNetPacketHeader&, TConstArrayView<uint8> ReqBytes) -> TOptional<TArray<uint8>> {
            auto Req = NetService.GetSerializer()->DeserializeStruct<FGameCloudSaveDownloadReq>(ReqBytes);
            if (Req.IsFailure()) {
                return NullOpt;
            }
            FGameCloudSaveDownloadRsp Rsp;
            Rsp.SlotName = Req.GetValue().SlotName;
            if (const FGameCloudSaveUploadReq* Found = CloudStore.Find(Req.GetValue().SlotName)) {
                Rsp.bSuccess = true;
                Rsp.ArchiveVersion = Found->ArchiveVersion;
                Rsp.SaveBytes = Found->SaveBytes;
            }
            auto Enc = NetService.GetSerializer()->SerializeStruct(Rsp);
            return Enc.IsSuccess() ? Enc.GetValue() : TOptional<TArray<uint8>>{};
        }
    );
    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_QUERY_REQ, NET_MSG_CLOUD_SAVE_QUERY_RSP,
        [&CloudStore, &NetService]([[maybe_unused]] const FNetPacketHeader&, TConstArrayView<uint8> ReqBytes) -> TOptional<TArray<uint8>> {
            auto Req = NetService.GetSerializer()->DeserializeStruct<FGameCloudSaveQueryReq>(ReqBytes);
            if (Req.IsFailure()) {
                return NullOpt;
            }
            FGameCloudSaveQueryRsp Rsp;
            Rsp.bSuccess = true;
            Rsp.SlotName = Req.GetValue().SlotName;
            if (const FGameCloudSaveUploadReq* Found = CloudStore.Find(Req.GetValue().SlotName)) {
                Rsp.bExistsOnCloud = true;
                Rsp.SaveSizeBytes = Found->SaveBytes.Num();
            }
            auto Enc = NetService.GetSerializer()->SerializeStruct(Rsp);
            return Enc.IsSuccess() ? Enc.GetValue() : TOptional<TArray<uint8>>{};
        }
    );

    int32 Gold = 123456;
    FGameSaveSegment Seg;
    Seg.SegmentName = FName(TEXT("PerfEconomy"));
    Seg.SegmentVersion = 1;
    Seg.Save = [&Gold](FArchive& Ar) { Ar << Gold; };
    Seg.Load = [&Gold](FArchive& Ar, int32) { Ar << Gold; return !Ar.IsError(); };
    TestTrue(TEXT("注册云存档基准段成功"), SaveSys->RegisterSegment(Seg).IsSuccess());

    const FString SlotName = TEXT("Perf_CloudSlot");
    TestTrue(TEXT("预写本地基准存档成功"), SaveSys->SaveGameToSlot(SlotName).IsSuccess());

    const FBenchmarkMetrics UploadMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, 25,
        [&CloudSave, &NetService, &SlotName](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                EGameCloudSaveResult Res = EGameCloudSaveResult::NetworkError;
                CloudSave.UploadSlotAsync(SlotName, 5.0f, [&Res](EGameCloudSaveResult R, const FGameCloudSaveUploadRsp&) { Res = R; });
                FGameThreadTaskRunner::FlushInFlightTasks();
                NetService.Tick(0.01f);
                FGameThreadTaskRunner::FlushInFlightTasks();
                check(Res == EGameCloudSaveResult::Success);
            }
        }
    );

    const FBenchmarkMetrics DownloadMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, 25,
        [&CloudSave, &NetService, &SlotName](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                EGameCloudSaveResult Res = EGameCloudSaveResult::NetworkError;
                CloudSave.DownloadSlotAsync(SlotName, true, 5.0f, [&Res](EGameCloudSaveResult R, const FGameCloudSaveDownloadRsp&) { Res = R; });
                FGameThreadTaskRunner::FlushInFlightTasks();
                NetService.Tick(0.01f);
                FGameThreadTaskRunner::FlushInFlightTasks();
                check(Res == EGameCloudSaveResult::Success);
            }
        }
    );

    const FBenchmarkMetrics QueryMetrics = RunBenchmark(
        DefaultWarmupRounds, DefaultSampleRounds, 100,
        [&CloudSave, &NetService, &SlotName](const int32 Iters) {
            for (int32 i = 0; i < Iters; ++i) {
                EGameCloudSaveResult Res = EGameCloudSaveResult::NetworkError;
                CloudSave.QueryCloudSlotMetadataAsync(SlotName, 5.0f, [&Res](EGameCloudSaveResult R, const FGameCloudSaveQueryRsp&) { Res = R; });
                NetService.Tick(0.01f);
                check(Res == EGameCloudSaveResult::Success);
            }
        }
    );

    AddInfo(FString::Printf(
        TEXT("CloudSave (MockNet): Upload=%0.2f ms/op (%0.1f ops/s) | Download+Validate+Apply=%0.2f ms/op (%0.1f ops/s) | Query=%0.2f us/op (%0.1f kops/s)"),
        UploadMetrics.MedianNs / 1.0e6, UploadMetrics.OpsPerSec,
        DownloadMetrics.MedianNs / 1.0e6, DownloadMetrics.OpsPerSec,
        QueryMetrics.MedianNs / 1000.0, QueryMetrics.OpsPerSec / 1000.0
    ));

    TestTrue(TEXT("云上传端到端满足预算 (< 15 ms)"), UploadMetrics.MedianNs < 15.0e6);
    TestTrue(TEXT("云下载先验后落盘+应用满足预算 (< 15 ms)"), DownloadMetrics.MedianNs < 15.0e6);
    TestTrue(TEXT("云元信息查询满足预算 (< 2 ms)"), QueryMetrics.MedianNs < 2.0e6);

    (void)SaveSys->DeleteSaveSlot(SlotName);
    (void)SaveSys->UnregisterSegment(Seg.SegmentName);
    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
