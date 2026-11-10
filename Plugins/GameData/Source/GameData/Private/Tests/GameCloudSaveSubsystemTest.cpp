// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloud/GameCloudSaveSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Message/GameNetworkMessageTypes.h"
#include "Misc/AutomationTest.h"
#include "Save/FSaveGameFileIO.h"
#include "Save/USaveGameSubsystem.h"
#include "System/GameNetServiceSubsystem.h"
#include "Transport/MockNetTransport.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCloudSaveSubsystemTest,
    "Game.Data.CloudSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证云存档子系统端到端闭环：分段落盘、上传云端、删本地档、云端拉取、坏档拦截与自动恢复
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
    TestNotNull(TEXT("本地持久化子系统应就绪"), SaveSys);
    if (SaveSys == nullptr) {
        return false;
    }

    const TSharedRef<FMockNetTransport> MockTransport = MakeShared<FMockNetTransport>();
    NetService.SetTransport(MockTransport);
    NetService.Connect(TEXT("mock://cloud-save-cluster"));
    TestTrue(TEXT("网络门面应成功连接至模拟端点"), NetService.IsConnected());

    TMap<FString, FGameCloudSaveUploadReq> MockCloudStorage;

    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_UPLOAD_REQ,
        NET_MSG_CLOUD_SAVE_UPLOAD_RSP,
        [&MockCloudStorage, &NetService](
            [[maybe_unused]] const FNetPacketHeader& Header,
            TConstArrayView<uint8> ReqBytes
        ) -> TOptional<TArray<uint8>> {
            const TGameStatusResult<FGameCloudSaveUploadReq> Req =
                NetService.GetSerializer()->DeserializeStruct<FGameCloudSaveUploadReq>(ReqBytes);
            if (Req.IsFailure()) {
                return NullOpt;
            }

            MockCloudStorage.Add(Req.GetValue().SlotName, Req.GetValue());

            FGameCloudSaveUploadRsp Rsp;
            Rsp.bSuccess = true;
            Rsp.SlotName = Req.GetValue().SlotName;
            Rsp.ErrorCode = 0;
            Rsp.CloudTimestamp = Req.GetValue().SaveTimestamp;

            const TGameStatusResult<TArray<uint8>> EncodedRsp = NetService.GetSerializer()->SerializeStruct(Rsp);
            return EncodedRsp.IsSuccess() ? EncodedRsp.GetValue() : TOptional<TArray<uint8>>{};
        }
    );

    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_DOWNLOAD_REQ,
        NET_MSG_CLOUD_SAVE_DOWNLOAD_RSP,
        [&MockCloudStorage, &NetService](
            [[maybe_unused]] const FNetPacketHeader& Header,
            TConstArrayView<uint8> ReqBytes
        ) -> TOptional<TArray<uint8>> {
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
            return EncodedRsp.IsSuccess() ? EncodedRsp.GetValue() : TOptional<TArray<uint8>>{};
        }
    );

    MockTransport->RegisterMockHandler(
        NET_MSG_CLOUD_SAVE_QUERY_REQ,
        NET_MSG_CLOUD_SAVE_QUERY_RSP,
        [&MockCloudStorage, &NetService](
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

            if (const FGameCloudSaveUploadReq* Found = MockCloudStorage.Find(Req.GetValue().SlotName)) {
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
        return true;
    };
    SaveSys->RegisterSegment(ProgressSegment);

    const FString TestSlot(TEXT("CloudSyncSlot"));
    TestTrue(TEXT("本地同步保存应成功"), SaveSys->SaveGameToSlot(TestSlot));
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
    NetService.Tick(0.05f);

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

    RuntimeGold = 0;
    TestTrue(TEXT("删除本地物理存档文件"), SaveSys->DeleteSaveSlot(TestSlot));
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
    NetService.Tick(0.05f);

    TestEqual(TEXT("下载结果应为 Success"), DownloadResult, EGameCloudSaveResult::Success);
    TestTrue(TEXT("云端响应字段 bSuccess 应为 true"), DownloadRsp.bSuccess);
    TestTrue(TEXT("下载并原子落盘后本地存档应已恢复"), SaveSys->DoesSaveSlotExist(TestSlot));
    TestEqual(TEXT("自动应用后内存段数值应 100% 恢复"), RuntimeGold, 88888);

    EGameCloudSaveResult NonexistentResult = EGameCloudSaveResult::Success;
    CloudSave.DownloadSlotAsync(
        TEXT("NoSuchSlotInCloud"),
        false,
        5.0f,
        [&NonexistentResult](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            NonexistentResult = Result;
        }
    );
    NetService.Tick(0.05f);
    TestEqual(TEXT("下载不存在槽位应返回 SlotNotFound"), NonexistentResult, EGameCloudSaveResult::SlotNotFound);

    MockCloudStorage.Find(TestSlot)->SaveBytes = {0x00, 0x01, 0x02, 0x03};
    EGameCloudSaveResult CorruptResult = EGameCloudSaveResult::Success;
    CloudSave.DownloadSlotAsync(
        TestSlot,
        false,
        5.0f,
        [&CorruptResult](EGameCloudSaveResult Result, const FGameCloudSaveDownloadRsp&) {
            CorruptResult = Result;
        }
    );
    NetService.Tick(0.05f);
    TestEqual(TEXT("云端坏包应在落盘前被拦截并报告 CorruptedData"), CorruptResult, EGameCloudSaveResult::CorruptedData);

    EGameCloudSaveResult InvalidSlotResult = EGameCloudSaveResult::Success;
    CloudSave.UploadSlotAsync(
        TEXT("bad/slot*name"),
        5.0f,
        [&InvalidSlotResult](EGameCloudSaveResult Result, const FGameCloudSaveUploadRsp&) {
            InvalidSlotResult = Result;
        }
    );
    TestEqual(TEXT("非法路径槽位名应被前置拦截并返回 SlotNotFound"), InvalidSlotResult, EGameCloudSaveResult::SlotNotFound);

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

    SaveSys->DeleteSaveSlot(TestSlot);
    SaveSys->UnregisterSegment(FName(TEXT("PlayerEconomy")));
    GameInstance->Shutdown();
    return true;
}

#endif // WITH_AUTOMATION_TESTS

