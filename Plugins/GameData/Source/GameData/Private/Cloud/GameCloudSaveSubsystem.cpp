// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloud/GameCloudSaveSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Message/GameNetworkMessageTypes.h"
#include "Save/FSaveGameFileIO.h"
#include "Save/USaveGameSubsystem.h"
#include "System/GameNetServiceSubsystem.h"

/**
 * @brief 异步上传本地存档快照至云端实现
 * @param SlotName 槽位名称
 * @param TimeoutSeconds 超时秒数
 * @param Callback 完成回调
 */
void UGameCloudSaveSubsystem::UploadSlotAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudUploadCompleted Callback
) {
    auto NotifyFailure = [this, &SlotName, &Callback](
        EGameCloudSaveResult Result,
        int32 ErrorCode,
        const FString& ErrorMessage
    ) {
        FGameCloudSaveUploadRsp FailedRsp;
        FailedRsp.SlotName = SlotName;
        FailedRsp.ErrorCode = ErrorCode;
        FailedRsp.ErrorMessage = ErrorMessage;

        OnCloudUploadComplete.Broadcast(SlotName, Result, ErrorMessage);
        if (Callback) {
            Callback(Result, FailedRsp);
        }
    };

    if (!FSaveGameFileIO::IsSlotNameAllowed(SlotName, {})) {
        NotifyFailure(EGameCloudSaveResult::SlotNotFound, 1, TEXT("InvalidSlotName"));
        return;
    }

    TArray<uint8> LocalBytes;
    if (!FSaveGameFileIO::ReadSlotBytes(SlotName, LocalBytes) || LocalBytes.IsEmpty()) {
        NotifyFailure(EGameCloudSaveResult::SlotNotFound, 2, TEXT("LocalSaveNotFound"));
        return;
    }

    if (!FSaveGameFileIO::HasValidSaveGameHeader(LocalBytes, SlotName)) {
        NotifyFailure(EGameCloudSaveResult::CorruptedData, 3, TEXT("CorruptedLocalSaveHeader"));
        return;
    }

    UGameNetServiceSubsystem* NetService = UGameNetServiceSubsystem::GetPtr(this);
    if (NetService == nullptr || !NetService->IsConnected()) {
        NotifyFailure(EGameCloudSaveResult::NetworkError, 4, TEXT("NetServiceNotConnected"));
        return;
    }

    FGameCloudSaveUploadReq Req;
    Req.SlotName = SlotName;
    Req.ArchiveVersion = 1;
    Req.SaveTimestamp = FDateTime::UtcNow();
    Req.SaveBytes = MoveTemp(LocalBytes);

    TWeakObjectPtr<UGameCloudSaveSubsystem> WeakThis(this);
    auto OnUploadResponse = [WeakThis, SlotName, Callback = MoveTemp(Callback)](
        ENetRequestResult NetResult,
        const FGameCloudSaveUploadRsp* ResponsePtr
    ) {
        EGameCloudSaveResult FinalResult = EGameCloudSaveResult::NetworkError;
        FGameCloudSaveUploadRsp FinalRsp;
        FinalRsp.SlotName = SlotName;

        if (NetResult == ENetRequestResult::Success && ResponsePtr != nullptr) {
            FinalRsp = *ResponsePtr;
            FinalResult = FinalRsp.bSuccess ? EGameCloudSaveResult::Success : EGameCloudSaveResult::NetworkError;
        } else if (NetResult == ENetRequestResult::TimedOut) {
            FinalRsp.ErrorCode = static_cast<int32>(ENetRequestResult::TimedOut);
            FinalRsp.ErrorMessage = TEXT("RequestTimeout");
        } else {
            FinalRsp.ErrorCode = static_cast<int32>(NetResult);
            FinalRsp.ErrorMessage = TEXT("NetworkSendFailed");
        }

        if (UGameCloudSaveSubsystem* Self = WeakThis.Get()) {
            Self->OnCloudUploadComplete.Broadcast(SlotName, FinalResult, FinalRsp.ErrorMessage);
        }

        if (Callback) {
            Callback(FinalResult, FinalRsp);
        }
    };

    NetService->SendStructRequest<FGameCloudSaveUploadReq, FGameCloudSaveUploadRsp>(
        NET_MSG_CLOUD_SAVE_UPLOAD_REQ,
        NET_MSG_CLOUD_SAVE_UPLOAD_RSP,
        Req,
        TimeoutSeconds,
        MoveTemp(OnUploadResponse)
    );
}

/**
 * @brief 异步拉取云端存档并原子落盘实现
 * @param SlotName 槽位名称
 * @param bAutoApplyToSubsystem 是否自动应用至段
 * @param TimeoutSeconds 超时秒数
 * @param Callback 完成回调
 */
void UGameCloudSaveSubsystem::DownloadSlotAsync(
    const FString& SlotName,
    bool bAutoApplyToSubsystem,
    float TimeoutSeconds,
    FOnCloudDownloadCompleted Callback
) {
    auto NotifyFailure = [this, &SlotName, &Callback](
        EGameCloudSaveResult Result,
        int32 ErrorCode,
        const FString& ErrorMessage
    ) {
        FGameCloudSaveDownloadRsp FailedRsp;
        FailedRsp.SlotName = SlotName;
        FailedRsp.ErrorCode = ErrorCode;
        FailedRsp.ErrorMessage = ErrorMessage;

        OnCloudDownloadComplete.Broadcast(SlotName, Result, ErrorMessage);
        if (Callback) {
            Callback(Result, FailedRsp);
        }
    };

    if (!FSaveGameFileIO::IsSlotNameAllowed(SlotName, {})) {
        NotifyFailure(EGameCloudSaveResult::SlotNotFound, 1, TEXT("InvalidSlotName"));
        return;
    }

    UGameNetServiceSubsystem* NetService = UGameNetServiceSubsystem::GetPtr(this);
    if (NetService == nullptr || !NetService->IsConnected()) {
        NotifyFailure(EGameCloudSaveResult::NetworkError, 2, TEXT("NetServiceNotConnected"));
        return;
    }

    FGameCloudSaveDownloadReq Req;
    Req.SlotName = SlotName;

    TWeakObjectPtr<UGameCloudSaveSubsystem> WeakThis(this);
    auto OnDownloadResponse = [WeakThis, SlotName, bAutoApplyToSubsystem, Callback = MoveTemp(Callback)](
        ENetRequestResult NetResult,
        const FGameCloudSaveDownloadRsp* ResponsePtr
    ) {
        auto NotifyComplete = [&WeakThis, &SlotName, &Callback](
            EGameCloudSaveResult Result,
            const FGameCloudSaveDownloadRsp& Rsp
        ) {
            UGameCloudSaveSubsystem* Self = WeakThis.Get();
            if (Self != nullptr) {
                Self->OnCloudDownloadComplete.Broadcast(SlotName, Result, Rsp.ErrorMessage);
            }
            if (Callback) {
                Callback(Result, Rsp);
            }
        };

        FGameCloudSaveDownloadRsp FinalRsp;
        FinalRsp.SlotName = SlotName;

        if (NetResult != ENetRequestResult::Success || ResponsePtr == nullptr) {
            FinalRsp.ErrorCode = static_cast<int32>(NetResult);
            FinalRsp.ErrorMessage = TEXT("DownloadNetworkFailed");
            NotifyComplete(EGameCloudSaveResult::NetworkError, FinalRsp);
            return;
        }

        if (!ResponsePtr->bSuccess) {
            FinalRsp = *ResponsePtr;
            const EGameCloudSaveResult Result = (FinalRsp.ErrorCode == 404)
                ? EGameCloudSaveResult::SlotNotFound : EGameCloudSaveResult::NetworkError;
            NotifyComplete(Result, FinalRsp);
            return;
        }

        if (!FSaveGameFileIO::HasValidSaveGameHeader(ResponsePtr->SaveBytes, SlotName)) {
            FinalRsp = *ResponsePtr;
            FinalRsp.ErrorCode = 3;
            FinalRsp.ErrorMessage = TEXT("CorruptedCloudPayload");
            NotifyComplete(EGameCloudSaveResult::CorruptedData, FinalRsp);
            return;
        }

        FinalRsp = *ResponsePtr;
        const uint64 Gen = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
        if (!FSaveGameFileIO::WriteSlotBytes(SlotName, FinalRsp.SaveBytes, Gen)) {
            FinalRsp.ErrorMessage = TEXT("WriteSlotBytesFailed");
            NotifyComplete(EGameCloudSaveResult::CorruptedData, FinalRsp);
            return;
        }

        UGameCloudSaveSubsystem* Self = WeakThis.Get();
        if (bAutoApplyToSubsystem && Self != nullptr) {
            Self->ApplySlotToSubsystem(SlotName);
        }

        NotifyComplete(EGameCloudSaveResult::Success, FinalRsp);
    };

    NetService->SendStructRequest<FGameCloudSaveDownloadReq, FGameCloudSaveDownloadRsp>(
        NET_MSG_CLOUD_SAVE_DOWNLOAD_REQ,
        NET_MSG_CLOUD_SAVE_DOWNLOAD_RSP,
        Req,
        TimeoutSeconds,
        MoveTemp(OnDownloadResponse)
    );
}

/**
 * @brief 异步查询云端存档元数据实现
 * @param SlotName 槽位名称
 * @param TimeoutSeconds 超时秒数
 * @param Callback 完成回调
 */
void UGameCloudSaveSubsystem::QueryCloudSlotMetadataAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudQueryCompleted Callback
) {
    if (!FSaveGameFileIO::IsSlotNameAllowed(SlotName, {})) {
        if (Callback) {
            FGameCloudSaveQueryRsp FailedRsp;
            FailedRsp.SlotName = SlotName;
            Callback(EGameCloudSaveResult::SlotNotFound, FailedRsp);
        }
        return;
    }

    UGameNetServiceSubsystem* NetService = UGameNetServiceSubsystem::GetPtr(this);
    if (NetService == nullptr || !NetService->IsConnected()) {
        if (Callback) {
            FGameCloudSaveQueryRsp FailedRsp;
            FailedRsp.SlotName = SlotName;
            Callback(EGameCloudSaveResult::NetworkError, FailedRsp);
        }
        return;
    }

    FGameCloudSaveQueryReq Req;
    Req.SlotName = SlotName;

    auto OnQueryResponse = [SlotName, Callback = MoveTemp(Callback)](
        ENetRequestResult NetResult,
        const FGameCloudSaveQueryRsp* ResponsePtr
    ) {
        if (NetResult != ENetRequestResult::Success || ResponsePtr == nullptr) {
            if (Callback) {
                FGameCloudSaveQueryRsp FailedRsp;
                FailedRsp.SlotName = SlotName;
                Callback(EGameCloudSaveResult::NetworkError, FailedRsp);
            }
            return;
        }

        const EGameCloudSaveResult Result = (ResponsePtr->bSuccess && ResponsePtr->bExistsOnCloud)
            ? EGameCloudSaveResult::Success : EGameCloudSaveResult::SlotNotFound;

        if (Callback) {
            Callback(Result, *ResponsePtr);
        }
    };

    NetService->SendStructRequest<FGameCloudSaveQueryReq, FGameCloudSaveQueryRsp>(
        NET_MSG_CLOUD_SAVE_QUERY_REQ,
        NET_MSG_CLOUD_SAVE_QUERY_RSP,
        Req,
        TimeoutSeconds,
        MoveTemp(OnQueryResponse)
    );
}

/**
 * @brief 蓝图便捷上传实现
 * @param SlotName 槽位名称
 * @param TimeoutSeconds 超时秒数
 */
void UGameCloudSaveSubsystem::UploadSlot(const FString& SlotName, float TimeoutSeconds) {
    UploadSlotAsync(SlotName, TimeoutSeconds, nullptr);
}

/**
 * @brief 蓝图便捷下载实现
 * @param SlotName 槽位名称
 * @param bAutoApplyToSubsystem 是否自动应用
 * @param TimeoutSeconds 超时秒数
 */
void UGameCloudSaveSubsystem::DownloadSlot(
    const FString& SlotName,
    bool bAutoApplyToSubsystem,
    float TimeoutSeconds
) {
    DownloadSlotAsync(SlotName, bAutoApplyToSubsystem, TimeoutSeconds, nullptr);
}

/**
 * @brief 安全获取云存档子系统指针实现
 * @param WorldContextObject 世界上下文
 * @return 子系统指针
 */
UGameCloudSaveSubsystem* UGameCloudSaveSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UGameInstance* DirectInstance = Cast<UGameInstance>(WorldContextObject)) {
        return DirectInstance->GetSubsystem<UGameCloudSaveSubsystem>();
    }
    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr || World->GetGameInstance() == nullptr) {
        return nullptr;
    }
    return World->GetGameInstance()->GetSubsystem<UGameCloudSaveSubsystem>();
}

/**
 * @brief 获取云存档子系统引用实现
 * @param WorldContextObject 世界上下文
 * @return 子系统引用
 */
UGameCloudSaveSubsystem& UGameCloudSaveSubsystem::Get(const UObject* WorldContextObject) {
    UGameCloudSaveSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 将下载落盘成功的存档安全热应用至本地已注册内存段实现
 * @param SlotName 目标存档槽位名称
 */
void UGameCloudSaveSubsystem::ApplySlotToSubsystem(const FString& SlotName) const {
    const UGameInstance* GI = GetGameInstance();
    if (GI == nullptr) {
        return;
    }
    USaveGameSubsystem* SaveSubsystem = GI->GetSubsystem<USaveGameSubsystem>();
    if (SaveSubsystem != nullptr) {
        SaveSubsystem->LoadGameFromSlot(SlotName);
    }
}
