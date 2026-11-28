// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameCloudSaveSubsystemTest.cpp
 * @brief 云端存档同步子系统全链路自动化测试（含先验后落盘防本地破坏、409/500/超时/取消与蓝图多播）
 */

#include "Cloud/GameCloudSaveSubsystem.h"

#include "Async/FCancellationTokenSource.h"
#include "Async/FGameThreadTaskRunner.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Message/GameNetworkMessageTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Save/FSaveGameFileIO.h"
#include "Save/USaveGameSubsystem.h"
#include "System/GameNetServiceSubsystem.h"
#include "Tests/GameCloudSaveTestTypes.h"
#include "Transport/MockNetTransport.h"

/**
 * @brief 接收并记录云存档上传完成多播事件
 * @param SlotName 存档槽位名称
 * @param Result 上传结果状态
 * @param ErrorMessage 错误详情
 */
void UGameCloudSaveTestListener::HandleUploadComplete(
    [[maybe_unused]] const FString& SlotName,
    EGameCloudSaveResult Result,
    [[maybe_unused]] const FString& ErrorMessage
) {
    UploadResults.Add(Result);
}

/**
 * @brief 接收并记录云存档下载完成多播事件
 * @param SlotName 存档槽位名称
 * @param Result 下载结果状态
 * @param ErrorMessage 错误详情
 */
void UGameCloudSaveTestListener::HandleDownloadComplete(
    [[maybe_unused]] const FString& SlotName,
    EGameCloudSaveResult Result,
    [[maybe_unused]] const FString& ErrorMessage
) {
    DownloadResults.Add(Result);
}

#if WITH_AUTOMATION_TESTS

namespace {

/**
 * @brief 推进异步后台任务与网络子系统 Tick
 * @param NetService 网络服务子系统引用
 * @param DeltaSeconds 模拟经过的秒数
 */
void PumpCloudSave(UGameNetServiceSubsystem& NetService, float DeltaSeconds = 0.05f) {
    FGameThreadTaskRunner::FlushInFlightTasks();
    NetService.Tick(DeltaSeconds);
    FGameThreadTaskRunner::FlushInFlightTasks();
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCloudSaveSubsystemTest,
    "Game.Data.CloudSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证云存档子系统端到端闭环：上传、查询、拉取、先验后落盘防本地破坏、409/500/超时/取消与蓝图多播
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameCloudSaveSubsystemTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameCloudSaveSubsystemTestWorld")));

    UGameCloudSaveSubsystem& CloudSave = UGameCloudSaveSubsystem::Get(GameInstance);
    USaveGameSubsystem* SaveSys = GameInstance->GetSubsystem<USaveGameSubsystem>();
    UGameNetServiceSubsystem& NetService = UGameNetServiceSubsystem::Get(GameInstance);

    TestEqual(
        TEXT("通过 World 解析的云存档子系统应与 GameInstance 完全一致"),
        UGameCloudSaveSubsystem::GetPtr(GameInstance->GetWorld()),
        &CloudSave
    );
    TestEqual(
        TEXT("通过同级 UGameInstanceSubsystem 解析的云存档子系统应一致"),
        UGameCloudSaveSubsystem::GetPtr(SaveSys),
        &CloudSave
    );
    TestNotNull(TEXT("本地持久化子系统应就绪"), SaveSys);
    if (SaveSys == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    const TSharedRef<FMockNetTransport> MockTransport = MakeShared<FMockNetTransport>();
    NetService.SetTransport(MockTransport);
    NetService.Connect(TEXT("mock://cloud-save-cluster"));
    TestTrue(TEXT("网络门面应成功连接至模拟端点"), NetService.IsConnected());

    TMap<FString, FGameCloudSaveUploadReq> MockCloudStorage;
    int32 ForcedUploadErrorCode = 0;
    int32 ForcedQueryErrorCode = 0;
    bool bDropDownloadRequest = false;
    bool bCaptureDownloadResponse = false;
    uint32 CapturedDownloadSequenceId = 0;
    TArray<uint8> CapturedDownloadRspBytes;
    TFunction<void()> OnDownloadRequestInFlight;

    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_UPLOAD_REQ,
        NET_MSG_CLOUD_SAVE_UPLOAD_RSP,
        [&MockCloudStorage, &NetService, &ForcedUploadErrorCode](
            [[maybe_unused]] const FNetPacketHeader& Header,
            TConstArrayView<uint8> ReqBytes
        ) -> TOptional<TArray<uint8>> {
            const TGameStatusResult<FGameCloudSaveUploadReq> Req =
                NetService.GetSerializer()->DeserializeStruct<FGameCloudSaveUploadReq>(ReqBytes);
            if (Req.IsFailure()) {
                return NullOpt;
            }

            FGameCloudSaveUploadRsp Rsp;
            Rsp.SlotName = Req.GetValue().SlotName;
            if (ForcedUploadErrorCode != 0) {
                Rsp.bSuccess = false;
                Rsp.ErrorCode = ForcedUploadErrorCode;
                Rsp.ErrorMessage = TEXT("ForcedServerError");
            } else {
                MockCloudStorage.Add(Req.GetValue().SlotName, Req.GetValue());
                Rsp.bSuccess = true;
                Rsp.ErrorCode = 0;
                Rsp.CloudTimestamp = Req.GetValue().SaveTimestamp;
            }

            const TGameStatusResult<TArray<uint8>> EncodedRsp = NetService.GetSerializer()->SerializeStruct(Rsp);
            return EncodedRsp.IsSuccess() ? EncodedRsp.GetValue() : TOptional<TArray<uint8>>{};
        }
    );

    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_DOWNLOAD_REQ,
        NET_MSG_CLOUD_SAVE_DOWNLOAD_RSP,
        [&MockCloudStorage, &NetService, &bDropDownloadRequest, &bCaptureDownloadResponse,
            &CapturedDownloadSequenceId, &CapturedDownloadRspBytes, &OnDownloadRequestInFlight](
            const FNetPacketHeader& Header,
            TConstArrayView<uint8> ReqBytes
        ) -> TOptional<TArray<uint8>> {
            if (OnDownloadRequestInFlight) {
                OnDownloadRequestInFlight();
            }
            if (bDropDownloadRequest) {
                return NullOpt;
            }
            const TGameStatusResult<FGameCloudSaveDownloadReq> Req =
                NetService.GetSerializer()->DeserializeStruct<FGameCloudSaveDownloadReq>(ReqBytes);
            if (Req.IsFailure()) {
                return NullOpt;
            }

            FGameCloudSaveDownloadRsp Rsp;
            Rsp.SlotName = Req.GetValue().SlotName;
            if (const FGameCloudSaveUploadReq* Found = MockCloudStorage.Find(Req.GetValue().SlotName)) {
                Rsp.bSuccess = true;
                Rsp.ErrorCode = 0;
                Rsp.ArchiveVersion = Found->ArchiveVersion;
                Rsp.CloudTimestamp = Found->SaveTimestamp;
                Rsp.SaveBytes = Found->SaveBytes;
            } else {
                Rsp.bSuccess = false;
                Rsp.ErrorCode = 404;
                Rsp.ErrorMessage = TEXT("SlotNotFoundOnCloud");
            }

            const TGameStatusResult<TArray<uint8>> EncodedRsp = NetService.GetSerializer()->SerializeStruct(Rsp);
            if (bCaptureDownloadResponse && EncodedRsp.IsSuccess()) {
                CapturedDownloadSequenceId = Header.SequenceId;
                CapturedDownloadRspBytes = EncodedRsp.GetValue();
                return NullOpt;
            }
            return EncodedRsp.IsSuccess() ? EncodedRsp.GetValue() : TOptional<TArray<uint8>>{};
        }
    );

    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_QUERY_REQ,
        NET_MSG_CLOUD_SAVE_QUERY_RSP,
        [&MockCloudStorage, &NetService, &ForcedQueryErrorCode](
            [[maybe_unused]] const FNetPacketHeader& Header,
            TConstArrayView<uint8> ReqBytes
        ) -> TOptional<TArray<uint8>> {
            const TGameStatusResult<FGameCloudSaveQueryReq> Req =
                NetService.GetSerializer()->DeserializeStruct<FGameCloudSaveQueryReq>(ReqBytes);
            if (Req.IsFailure()) {
                return NullOpt;
            }

            FGameCloudSaveQueryRsp Rsp;
            Rsp.SlotName = Req.GetValue().SlotName;
            if (ForcedQueryErrorCode != 0) {
                Rsp.bSuccess = false;
                Rsp.ErrorCode = ForcedQueryErrorCode;
                Rsp.ErrorMessage = TEXT("QueryBackendUnavailable");
            } else if (const FGameCloudSaveUploadReq* Found = MockCloudStorage.Find(Req.GetValue().SlotName)) {
                Rsp.bSuccess = true;
                Rsp.bExistsOnCloud = true;
                Rsp.ArchiveVersion = Found->ArchiveVersion;
                Rsp.CloudTimestamp = Found->SaveTimestamp;
                Rsp.SaveSizeBytes = Found->SaveBytes.Num();
            } else {
                Rsp.bSuccess = true;
                Rsp.bExistsOnCloud = false;
            }

            const TGameStatusResult<TArray<uint8>> EncodedRsp = NetService.GetSerializer()->SerializeStruct(Rsp);
            return EncodedRsp.IsSuccess() ? EncodedRsp.GetValue() : TOptional<TArray<uint8>>{};
        }
    );

    int32 RuntimeGold = 88888;
    FGameSaveSegment ProgressSegment;
    ProgressSegment.SegmentName = FName(TEXT("PlayerEconomy"));
    ProgressSegment.SegmentVersion = 1;
    ProgressSegment.Save = [&RuntimeGold](FArchive& Ar) {
        Ar << RuntimeGold;
    };
    ProgressSegment.Load = [&RuntimeGold](FArchive& Ar, [[maybe_unused]] int32 StoredVersion) {
        Ar << RuntimeGold;
        return !Ar.IsError();
    };
    TestTrue(TEXT("注册测试段成功"), SaveSys->RegisterSegment(ProgressSegment).IsSuccess());

    UGameCloudSaveTestListener* Listener = NewObject<UGameCloudSaveTestListener>(GameInstance);
    CloudSave.OnCloudUploadComplete.AddDynamic(Listener, &UGameCloudSaveTestListener::HandleUploadComplete);
    CloudSave.OnCloudDownloadComplete.AddDynamic(Listener, &UGameCloudSaveTestListener::HandleDownloadComplete);

    const FString TestSlot(TEXT("CloudSyncSlot"));
    TestTrue(TEXT("本地同步保存应成功"), SaveSys->SaveGameToSlot(TestSlot).IsSuccess());
    TestTrue(TEXT("本地存档文件应存在"), SaveSys->DoesSaveSlotExist(TestSlot));

    EGameCloudSaveResult UploadResult = EGameCloudSaveResult::NetworkError;
    FGameCloudSaveUploadRsp UploadRsp;
    CloudSave.UploadSlotAsync(
        TestSlot,
        5.0f,
        [&UploadResult, &UploadRsp](EGameCloudSaveResult Result, const FGameCloudSaveUploadRsp& Rsp) {
            UploadResult = Result;
            UploadRsp = Rsp;
        }
    );
    PumpCloudSave(NetService);

    TestEqual(TEXT("上传结果应为 Success"), UploadResult, EGameCloudSaveResult::Success);
    TestTrue(TEXT("云端响应字段 bSuccess 应为 true"), UploadRsp.bSuccess);
    TestTrue(TEXT("模拟云端存储应包含该槽位"), MockCloudStorage.Contains(TestSlot));

    EGameCloudSaveResult QueryResult = EGameCloudSaveResult::NetworkError;
    FGameCloudSaveQueryRsp QueryRsp;
    CloudSave.QueryCloudSlotMetadataAsync(
        TestSlot,
        5.0f,
        [&QueryResult, &QueryRsp](EGameCloudSaveResult Result, const FGameCloudSaveQueryRsp& Rsp) {
            QueryResult = Result;
            QueryRsp = Rsp;
        }
    );
    NetService.Tick(0.05f);

    TestEqual(TEXT("元信息查询状态应为 Success"), QueryResult, EGameCloudSaveResult::Success);
    TestTrue(TEXT("元信息报告云端存在该槽位"), QueryRsp.bExistsOnCloud);
    TestTrue(TEXT("云端数据大小应大于 0"), QueryRsp.SaveSizeBytes > 0);

    ForcedQueryErrorCode = 500;
    EGameCloudSaveResult Query500Result = EGameCloudSaveResult::Success;
    CloudSave.QueryCloudSlotMetadataAsync(
        TestSlot,
        5.0f,
        [&Query500Result](EGameCloudSaveResult Result, const FGameCloudSaveQueryRsp&) {
            Query500Result = Result;
        }
    );
    NetService.Tick(0.05f);
    ForcedQueryErrorCode = 0;
    TestEqual(TEXT("查询接口返回 500 应准确映射为 NetworkError"), Query500Result, EGameCloudSaveResult::NetworkError);

    ForcedUploadErrorCode = 409;
    EGameCloudSaveResult ConflictUploadResult = EGameCloudSaveResult::Success;
    CloudSave.UploadSlotAsync(
        TestSlot,
        5.0f,
        [&ConflictUploadResult](EGameCloudSaveResult Result, const FGameCloudSaveUploadRsp&) {
            ConflictUploadResult = Result;
        }
    );
    PumpCloudSave(NetService);
    ForcedUploadErrorCode = 0;
    TestEqual(TEXT("上传接口返回 409 应映射为 VersionMismatch"), ConflictUploadResult, EGameCloudSaveResult::VersionMismatch);

    RuntimeGold = 0;
    TestTrue(TEXT("删除本地物理存档文件"), SaveSys->DeleteSaveSlot(TestSlot).IsSuccess());
    TestFalse(TEXT("本地存档应已被彻底移除"), SaveSys->DoesSaveSlotExist(TestSlot));

    EGameCloudSaveResult DownloadResult = EGameCloudSaveResult::NetworkError;
    FGameCloudSaveDownloadRsp DownloadRsp;
    CloudSave.DownloadSlotAsync(
        TestSlot,
        true,
        5.0f,
        [&DownloadResult, &DownloadRsp](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp& Rsp) {
            DownloadResult = Result;
            DownloadRsp = Rsp;
        }
    );
    PumpCloudSave(NetService);

    TestEqual(TEXT("下载结果应为 Success"), DownloadResult, EGameCloudSaveResult::Success);
    TestTrue(TEXT("云端响应字段 bSuccess 应为 true"), DownloadRsp.bSuccess);
    TestTrue(TEXT("下载并原子落盘后本地存档应已恢复"), SaveSys->DoesSaveSlotExist(TestSlot));
    TestEqual(TEXT("自动应用后内存段数值应 100% 恢复"), RuntimeGold, 88888);

    auto HealthyLocalBytesRes = FSaveGameFileIO::ReadSlotBytes(TestSlot);
    TestTrue(TEXT("记录健康本地档基准字节成功"), HealthyLocalBytesRes.IsSuccess());
    const TArray<uint8> HealthyLocalBytes = HealthyLocalBytesRes.IsSuccess() ? HealthyLocalBytesRes.ExtractValue() : TArray<uint8>();

    TArray<uint8> PostHeaderCorruptBytes;
    PostHeaderCorruptBytes.Init(0xEE, 128);
    constexpr int32 SaveGameFileTypeTag = 0x53415647;
    FMemory::Memcpy(PostHeaderCorruptBytes.GetData(), &SaveGameFileTypeTag, sizeof(int32));
    MockCloudStorage.Find(TestSlot)->SaveBytes = PostHeaderCorruptBytes;

    AddExpectedErrorPlain(TEXT("存档反序列化失败"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("云端存档落盘前校验失败"), EAutomationExpectedErrorFlags::Contains, 1);
    EGameCloudSaveResult PostHeaderCorruptResult = EGameCloudSaveResult::Success;
    CloudSave.DownloadSlotAsync(
        TestSlot,
        true,
        5.0f,
        [&PostHeaderCorruptResult](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            PostHeaderCorruptResult = Result;
        }
    );
    PumpCloudSave(NetService);
    TestEqual(TEXT("头合法但正文损坏的云档应在写盘前拦截并返回 CorruptedData"),
        PostHeaderCorruptResult, EGameCloudSaveResult::CorruptedData);

    const auto AfterCorruptLocalBytesRes = FSaveGameFileIO::ReadSlotBytes(TestSlot);
    TestTrue(TEXT("云档反序列化失败后本地健康存档字节零篡改"),
        AfterCorruptLocalBytesRes.IsSuccess() && AfterCorruptLocalBytesRes.GetValue() == HealthyLocalBytes);

    UGameSaveArchive* IncompatibleArchive = NewObject<UGameSaveArchive>();
    IncompatibleArchive->Metadata.SlotName = TestSlot;
    IncompatibleArchive->Metadata.ArchiveVersion = UGameSaveArchive::CurrentArchiveVersion;
    FGameSaveSegmentPayload HighVerPayload;
    HighVerPayload.SegmentVersion = 99;
    IncompatibleArchive->Segments.Add(FName(TEXT("PlayerEconomy")), HighVerPayload);

    auto IncompatSerializeRes = FSaveGameFileIO::SerializeObject(IncompatibleArchive);
    TestTrue(TEXT("序列化高版本测试存档成功"), IncompatSerializeRes.IsSuccess());
    MockCloudStorage.Find(TestSlot)->SaveBytes = IncompatSerializeRes.IsSuccess() ? IncompatSerializeRes.ExtractValue() : TArray<uint8>();

    AddExpectedErrorPlain(TEXT("高于当前段版本"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("云端存档落盘前校验失败"), EAutomationExpectedErrorFlags::Contains, 1);
    EGameCloudSaveResult IncompatibleResult = EGameCloudSaveResult::Success;
    FGameCloudSaveDownloadRsp IncompatibleRsp;
    CloudSave.DownloadSlotAsync(
        TestSlot,
        true,
        5.0f,
        [&IncompatibleResult, &IncompatibleRsp](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp& Rsp) {
            IncompatibleResult = Result;
            IncompatibleRsp = Rsp;
        }
    );
    PumpCloudSave(NetService);
    TestEqual(TEXT("高版本段应在写盘前拦截并报告 VersionMismatch"), IncompatibleResult, EGameCloudSaveResult::VersionMismatch);
    TestFalse(TEXT("落盘前校验失败时响应 bSuccess 应置为 false"), IncompatibleRsp.bSuccess);
    TestTrue(TEXT("落盘前校验失败时响应 ErrorCode 应为非零"), IncompatibleRsp.ErrorCode != 0);

    AddExpectedErrorPlain(TEXT("高于当前段版本"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("云端存档落盘前校验失败"), EAutomationExpectedErrorFlags::Contains, 1);
    EGameCloudSaveResult NoAutoApplyIncompatibleResult = EGameCloudSaveResult::Success;
    CloudSave.DownloadSlotAsync(
        TestSlot,
        false,
        5.0f,
        [&NoAutoApplyIncompatibleResult](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            NoAutoApplyIncompatibleResult = Result;
        }
    );
    PumpCloudSave(NetService);
    TestEqual(TEXT("bAutoApply=false 下载高版本段同样应在写盘前拦截并报告 VersionMismatch"),
        NoAutoApplyIncompatibleResult, EGameCloudSaveResult::VersionMismatch);

    RuntimeGold = 0;
    TestTrue(TEXT("高版本云档被拒后本地健康档完好且可完整重读"), SaveSys->LoadGameFromSlot(TestSlot).IsSuccess());
    TestEqual(TEXT("重读健康本地档仍恢复 88888"), RuntimeGold, 88888);

    const FString CorruptUploadSlot = TEXT("CorruptUploadSlot");
    const FString CorruptUploadPath = FSaveGameFileIO::GetSaveSlotFilePath(CorruptUploadSlot);
    TArray<uint8> BrokenLocalBytes;
    BrokenLocalBytes.Init(0, 64);
    TestTrue(TEXT("写入损坏本地档用于上传测试"), FFileHelper::SaveArrayToFile(BrokenLocalBytes, *CorruptUploadPath));
    AddExpectedErrorPlain(TEXT("本地存档已损坏"), EAutomationExpectedErrorFlags::Contains, 1);
    EGameCloudSaveResult CorruptUploadRes = EGameCloudSaveResult::Success;
    CloudSave.UploadSlotAsync(
        CorruptUploadSlot,
        5.0f,
        [&CorruptUploadRes](EGameCloudSaveResult Result, const FGameCloudSaveUploadRsp&) {
            CorruptUploadRes = Result;
        }
    );
    PumpCloudSave(NetService);
    TestEqual(TEXT("上传损坏本地档应精确返回 CorruptedData 而非 SlotNotFound"),
        CorruptUploadRes, EGameCloudSaveResult::CorruptedData);
    (void)FSaveGameFileIO::DeleteSaveSlot(CorruptUploadSlot);

    FCancellationTokenSource MidFlightCancel;
    OnDownloadRequestInFlight = [&MidFlightCancel]() {
        MidFlightCancel.Cancel();
    };
    EGameCloudSaveResult MidFlightCancelledRes = EGameCloudSaveResult::Success;
    CloudSave.DownloadSlotAsync(
        TestSlot,
        false,
        5.0f,
        [&MidFlightCancelledRes](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            MidFlightCancelledRes = Result;
        },
        MidFlightCancel.GetToken()
    );
    PumpCloudSave(NetService, 0.05f);
    OnDownloadRequestInFlight = nullptr;
    TestEqual(TEXT("在途请求期间取消外部令牌应返回 Cancelled"),
        MidFlightCancelledRes, EGameCloudSaveResult::Cancelled);

    EGameCloudSaveResult NonexistentResult = EGameCloudSaveResult::Success;
    CloudSave.DownloadSlotAsync(
        TEXT("NoSuchSlotInCloud"),
        false,
        5.0f,
        [&NonexistentResult](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            NonexistentResult = Result;
        }
    );
    PumpCloudSave(NetService);
    TestEqual(TEXT("下载不存在槽位应返回 SlotNotFound"), NonexistentResult, EGameCloudSaveResult::SlotNotFound);

    bDropDownloadRequest = true;
    EGameCloudSaveResult TimeoutResult = EGameCloudSaveResult::Success;
    CloudSave.DownloadSlotAsync(
        TestSlot,
        false,
        0.1f,
        [&TimeoutResult](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            TimeoutResult = Result;
        }
    );
    PumpCloudSave(NetService, 0.25f);
    bDropDownloadRequest = false;
    TestEqual(TEXT("云端下载请求超时应映射为 NetworkError"), TimeoutResult, EGameCloudSaveResult::NetworkError);

    FCancellationTokenSource PreCancelled;
    PreCancelled.Cancel();
    EGameCloudSaveResult CancelledUpload = EGameCloudSaveResult::Success;
    EGameCloudSaveResult CancelledDownload = EGameCloudSaveResult::Success;
    EGameCloudSaveResult CancelledQuery = EGameCloudSaveResult::Success;
    CloudSave.UploadSlotAsync(
        TestSlot, 5.0f,
        [&CancelledUpload](EGameCloudSaveResult Result, const FGameCloudSaveUploadRsp&) { CancelledUpload = Result; },
        PreCancelled.GetToken()
    );
    CloudSave.DownloadSlotAsync(
        TestSlot, true, 5.0f,
        [&CancelledDownload](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) { CancelledDownload = Result; },
        PreCancelled.GetToken()
    );
    CloudSave.QueryCloudSlotMetadataAsync(
        TestSlot, 5.0f,
        [&CancelledQuery](EGameCloudSaveResult Result, const FGameCloudSaveQueryRsp&) { CancelledQuery = Result; },
        PreCancelled.GetToken()
    );
    TestEqual(TEXT("预取消令牌触发上传 Cancelled"), CancelledUpload, EGameCloudSaveResult::Cancelled);
    TestEqual(TEXT("预取消令牌触发下载 Cancelled"), CancelledDownload, EGameCloudSaveResult::Cancelled);
    TestEqual(TEXT("预取消令牌触发查询 Cancelled"), CancelledQuery, EGameCloudSaveResult::Cancelled);

    Listener->UploadResults.Reset();
    Listener->DownloadResults.Reset();
    CloudSave.UploadSlot(TestSlot, 5.0f);
    PumpCloudSave(NetService);
    TestEqual(TEXT("蓝图 UploadSlot 应触发一次多播通知"), Listener->UploadResults.Num(), 1);
    if (Listener->UploadResults.Num() == 1) {
        TestEqual(TEXT("蓝图 UploadSlot 广播结果为 Success"), Listener->UploadResults[0], EGameCloudSaveResult::Success);
    }

    CloudSave.DownloadSlot(TestSlot, true, 5.0f);
    PumpCloudSave(NetService);
    TestEqual(TEXT("蓝图 DownloadSlot 应触发一次多播通知"), Listener->DownloadResults.Num(), 1);
    if (Listener->DownloadResults.Num() == 1) {
        TestEqual(TEXT("蓝图 DownloadSlot 广播结果为 Success"), Listener->DownloadResults[0], EGameCloudSaveResult::Success);
    }

    const FString DualSlotA = TEXT("CloudDualSlotA");
    const FString DualSlotB = TEXT("CloudDualSlotB");
    RuntimeGold = 11111;
    TestTrue(TEXT("保存 DualSlotA 本地档成功"), SaveSys->SaveGameToSlot(DualSlotA).IsSuccess());
    CloudSave.UploadSlot(DualSlotA, 5.0f);
    PumpCloudSave(NetService);
    RuntimeGold = 22222;
    TestTrue(TEXT("保存 DualSlotB 本地档成功"), SaveSys->SaveGameToSlot(DualSlotB).IsSuccess());
    CloudSave.UploadSlot(DualSlotB, 5.0f);
    PumpCloudSave(NetService);

    EGameCloudSaveResult DualResA = EGameCloudSaveResult::IoError;
    EGameCloudSaveResult DualResB = EGameCloudSaveResult::IoError;
    CloudSave.DownloadSlotAsync(
        DualSlotA,
        true,
        5.0f,
        [&DualResA](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            DualResA = Result;
        }
    );
    CloudSave.DownloadSlotAsync(
        DualSlotB,
        true,
        5.0f,
        [&DualResB](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            DualResB = Result;
        }
    );
    PumpCloudSave(NetService);
    TestEqual(TEXT("跨槽位同代次并发下载 DualSlotA 应成功且零串档碰撞"), DualResA, EGameCloudSaveResult::Success);
    TestEqual(TEXT("跨槽位同代次并发下载 DualSlotB 应成功且零串档碰撞"), DualResB, EGameCloudSaveResult::Success);
    (void)SaveSys->DeleteSaveSlot(DualSlotA);
    (void)SaveSys->DeleteSaveSlot(DualSlotB);

    EGameCloudSaveResult InvalidSlotResult = EGameCloudSaveResult::Success;
    CloudSave.UploadSlotAsync(
        TEXT("bad/slot*name"),
        5.0f,
        [&InvalidSlotResult](EGameCloudSaveResult Result, const FGameCloudSaveUploadRsp&) {
            InvalidSlotResult = Result;
        }
    );
    TestEqual(TEXT("非法路径槽位名应被前置拦截并返回 SlotNotFound"), InvalidSlotResult, EGameCloudSaveResult::SlotNotFound);

    const FString RaceSlot = TEXT("CloudRaceSlot");
    RuntimeGold = 11111;
    TestTrue(TEXT("RaceSlot 初始本地档保存成功"), SaveSys->SaveGameToSlot(RaceSlot).IsSuccess());
    CloudSave.UploadSlot(RaceSlot, 5.0f);
    PumpCloudSave(NetService);

    RuntimeGold = 22222;
    TestTrue(TEXT("RaceSlot 更新本地档保存成功"), SaveSys->SaveGameToSlot(RaceSlot).IsSuccess());
    PumpCloudSave(NetService);

    bCaptureDownloadResponse = true;
    CapturedDownloadSequenceId = 0;
    CapturedDownloadRspBytes.Reset();
    EGameCloudSaveResult RaceDownloadResult = EGameCloudSaveResult::Success;
    FGameCloudSaveDownloadRsp RaceDownloadRsp;
    CloudSave.DownloadSlotAsync(
        RaceSlot,
        true,
        5.0f,
        [&RaceDownloadResult, &RaceDownloadRsp](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp& Rsp) {
            RaceDownloadResult = Result;
            RaceDownloadRsp = Rsp;
        }
    );
    PumpCloudSave(NetService);
    bCaptureDownloadResponse = false;
    TestTrue(TEXT("应捕获到在途下载响应字节"), CapturedDownloadRspBytes.Num() > 0);

    RuntimeGold = 33333;
    TestTrue(TEXT("下载在途期间再次本地保存成功"), SaveSys->SaveGameToSlot(RaceSlot).IsSuccess());
    PumpCloudSave(NetService);

    TestTrue(
        TEXT("应成功注入旧云响应"),
        MockTransport->SimulateServerPush(
            NET_MSG_CLOUD_SAVE_DOWNLOAD_RSP,
            CapturedDownloadRspBytes,
            CapturedDownloadSequenceId
        )
    );
    PumpCloudSave(NetService);

    TestEqual(
        TEXT("请求在途期间被更新本地保存超越后，旧下载应返回 Cancelled"),
        RaceDownloadResult,
        EGameCloudSaveResult::Cancelled
    );
    TestFalse(TEXT("过期下载响应 bSuccess 应为 false"), RaceDownloadRsp.bSuccess);

    RuntimeGold = 0;
    TestTrue(TEXT("过期下载后本地档仍可加载"), SaveSys->LoadGameFromSlot(RaceSlot).IsSuccess());
    TestEqual(TEXT("本地新进度不得被旧云档覆盖"), RuntimeGold, 33333);
    (void)SaveSys->DeleteSaveSlot(RaceSlot);

    NetService.Disconnect();
    EGameCloudSaveResult DisconnectUploadResult = EGameCloudSaveResult::Success;
    CloudSave.UploadSlotAsync(
        TestSlot,
        5.0f,
        [&DisconnectUploadResult](EGameCloudSaveResult Result, const FGameCloudSaveUploadRsp&) {
            DisconnectUploadResult = Result;
        }
    );
    TestEqual(TEXT("断线状态下上传应立即返回 NetworkError"), DisconnectUploadResult, EGameCloudSaveResult::NetworkError);

    (void)SaveSys->DeleteSaveSlot(TestSlot);
    (void)SaveSys->UnregisterSegment(FName(TEXT("PlayerEconomy")));
    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
