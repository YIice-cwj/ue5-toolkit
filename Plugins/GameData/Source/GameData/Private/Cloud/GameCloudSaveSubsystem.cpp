// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameCloudSaveSubsystem.cpp
 * @brief 云端存档同步门面子系统实现
 */

#include "Cloud/GameCloudSaveSubsystem.h"

#include "Async/FGameThreadTaskRunner.h"
#include "Core/DataErrorCodes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/GameLogCategories.h"
#include "Message/GameNetworkMessageTypes.h"
#include "Save/FSaveGameFileIO.h"
#include "Save/GameSaveArchive.h"
#include "Save/USaveGameSubsystem.h"
#include "System/GameNetServiceSubsystem.h"

namespace {

constexpr int32 CloudHttpNotFoundCode = 404;
constexpr int32 CloudHttpConflictCode = 409;
constexpr int32 CloudErrInvalidSlot = 1;
constexpr int32 CloudErrLocalSlotMissing = 2;
constexpr int32 CloudErrCorruptedPayload = 3;
constexpr int32 CloudErrNetworkDisconnected = 4;
constexpr int32 CloudErrCancelled = 5;
constexpr int32 CloudErrDiskWriteFailed = 6;
constexpr int32 CloudErrVersionMismatch = 7;
constexpr int32 CloudErrLocalReadFailed = 8;
constexpr int32 CloudErrSuperseded = 9;

/**
 * @brief 校验指定槽位名称是否合法且符合本地存档白名单约束
 * @param Subsystem 云存档子系统实例指针
 * @param SlotName 待校验槽位名称
 * @return 允许使用返回 true
 */
bool CheckSlotAllowed(const UGameCloudSaveSubsystem* Subsystem, const FString& SlotName) {
    if (Subsystem != nullptr) {
        if (const UGameInstance* GI = Subsystem->GetGameInstance()) {
            if (const USaveGameSubsystem* SaveSys = GI->GetSubsystem<USaveGameSubsystem>()) {
                return SaveSys->IsSlotNameAllowed(SlotName);
            }
        }
    }
    return FSaveGameFileIO::IsSlotNameAllowed(SlotName, {});
}

/**
 * @brief 将上传响应中的服务端错误码映射为 EGameCloudSaveResult
 * @param Rsp 服务端返回的上传响应结构体
 * @return 对应的云存档同步结果状态
 */
EGameCloudSaveResult MapUploadResponseResult(const FGameCloudSaveUploadRsp& Rsp) {
    if (Rsp.bSuccess) {
        return EGameCloudSaveResult::Success;
    }
    if (Rsp.ErrorCode == CloudHttpConflictCode) {
        return EGameCloudSaveResult::VersionMismatch;
    }
    if (Rsp.ErrorCode == CloudHttpNotFoundCode) {
        return EGameCloudSaveResult::SlotNotFound;
    }
    return EGameCloudSaveResult::NetworkError;
}

/**
 * @brief 将下载校验阶段的 Game::DataErrorCodes 映射为 EGameCloudSaveResult
 * @param DataErrorCode 本地存档校验返回的错误码
 * @return 对应的云存档同步结果状态
 */
EGameCloudSaveResult MapArchiveValidationError(int32 DataErrorCode) {
    if (DataErrorCode == Game::DataErrorCodes::ArchiveVersionMismatch
        || DataErrorCode == Game::DataErrorCodes::SegmentVersionMismatch) {
        return EGameCloudSaveResult::VersionMismatch;
    }
    return EGameCloudSaveResult::CorruptedData;
}

}  // namespace

/**
 * @brief 子系统初始化生命周期回调：重置生命周期取消源
 * @param Collection 子系统依赖集合
 */
void UGameCloudSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    PendingDownloadArchives.Empty();
    NextPendingDownloadId = 0;
    LifetimeCancelSource = FCancellationTokenSource();
}

/**
 * @brief 子系统销毁生命周期回调：取消所有在途云端同步任务并清理待热应用归档缓存
 */
void UGameCloudSaveSubsystem::Deinitialize() {
    LifetimeCancelSource.Cancel();
    PendingDownloadArchives.Empty();
    Super::Deinitialize();
}

/**
 * @brief 安全获取云存档子系统指针（支持 UWorld、UGameInstance、UGameInstanceSubsystem 与 AActor）
 * @param WorldContextObject 世界或子系统上下文对象
 * @return 子系统实例指针，未就绪时返回 nullptr
 */
UGameCloudSaveSubsystem* UGameCloudSaveSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UGameInstance* DirectInstance = Cast<UGameInstance>(WorldContextObject)) {
        return DirectInstance->GetSubsystem<UGameCloudSaveSubsystem>();
    }
    if (const UGameInstanceSubsystem* Subsystem = Cast<UGameInstanceSubsystem>(WorldContextObject)) {
        if (const UGameInstance* GI = Subsystem->GetGameInstance()) {
            return GI->GetSubsystem<UGameCloudSaveSubsystem>();
        }
    }
    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr || World->GetGameInstance() == nullptr) {
        return nullptr;
    }
    return World->GetGameInstance()->GetSubsystem<UGameCloudSaveSubsystem>();
}

/**
 * @brief 获取云存档子系统引用（断言有效）
 * @param WorldContextObject 世界或子系统上下文对象
 * @return 子系统强引用
 */
UGameCloudSaveSubsystem& UGameCloudSaveSubsystem::Get(const UObject* WorldContextObject) {
    UGameCloudSaveSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 异步上传指定槽位的本地存档快照至远端云存储服务（C++ 强类型回调版本，支持协同取消）
 * @param SlotName 目标存档槽位名称
 * @param TimeoutSeconds 请求超时秒数
 * @param Callback 操作完成回调闭包
 * @param CancellationToken 可选外部协同取消令牌
 */
void UGameCloudSaveSubsystem::UploadSlotAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudUploadCompleted Callback,
    FCancellationToken CancellationToken
) {
    TSharedPtr<FCancellationTokenSource> LinkedSource;
    const FCancellationToken EffectiveToken = ResolveEffectiveToken(CancellationToken, LinkedSource);
    if (EffectiveToken.IsCancellationRequested()) {
        GAME_LOG(Log, TEXT("【云存档】上传请求在派发前已被取消: 槽位=%s"), *SlotName);
        NotifyUploadFailure(SlotName, EGameCloudSaveResult::Cancelled, CloudErrCancelled, TEXT("UploadCancelled"), Callback);
        return;
    }

    if (!CheckSlotAllowed(this, SlotName)) {
        GAME_LOG(Warning, TEXT("【云存档】上传被拒：槽位名称无效或不在白名单内: %s"), *SlotName);
        NotifyUploadFailure(SlotName, EGameCloudSaveResult::SlotNotFound, CloudErrInvalidSlot, TEXT("槽位名称无效或不在白名单内"), Callback);
        return;
    }

    UGameNetServiceSubsystem* NetService = UGameNetServiceSubsystem::GetPtr(this);
    if (NetService == nullptr || !NetService->IsConnected()) {
        GAME_LOG(Warning, TEXT("【云存档】上传被拒：网络服务未连接: 槽位=%s"), *SlotName);
        NotifyUploadFailure(SlotName, EGameCloudSaveResult::NetworkError, CloudErrNetworkDisconnected, TEXT("网络服务未连接"), Callback);
        return;
    }

    TWeakObjectPtr<UGameCloudSaveSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(
        FCancellationToken::None(),
        [WeakThis, SlotName, TimeoutSeconds, EffectiveToken, LinkedSource = MoveTemp(LinkedSource), Callback = MoveTemp(Callback)]() mutable {
            TGameStatusResult<TArray<uint8>> ReadResult = EffectiveToken.IsCancellationRequested()
                ? TGameStatusResult<TArray<uint8>>::Failure(
                    Game::ModuleId::Data,
                    Game::DataErrorCodes::SaveSlotNotFound,
                    TEXT("UploadCancelledBeforeRead")
                )
                : FSaveGameFileIO::ReadSlotBytes(SlotName);

            FGameThreadTaskRunner::RunOnGameThread(
                [WeakThis, SlotName, TimeoutSeconds, EffectiveToken, LinkedSource = MoveTemp(LinkedSource), ReadResult = MoveTemp(ReadResult), Callback = MoveTemp(Callback)]() mutable {
                    UGameCloudSaveSubsystem* Self = WeakThis.Get();
                    if (Self == nullptr) {
                        if (Callback) {
                            FGameCloudSaveUploadRsp CancelledRsp;
                            CancelledRsp.SlotName = SlotName;
                            CancelledRsp.bSuccess = false;
                            CancelledRsp.ErrorCode = CloudErrCancelled;
                            CancelledRsp.ErrorMessage = TEXT("SubsystemDestroyed");
                            Callback(EGameCloudSaveResult::Cancelled, CancelledRsp);
                        }
                        return;
                    }
                    Self->DispatchUploadRequest(
                        SlotName,
                        TimeoutSeconds,
                        MoveTemp(ReadResult),
                        MoveTemp(Callback),
                        EffectiveToken,
                        MoveTemp(LinkedSource)
                    );
                }
            );
        }
    );
}

/**
 * @brief 异步从云端拉取指定槽位存档、先内存反序列化与版本校验，通过后再原子写入本地磁盘并可选热应用
 * @param SlotName 目标存档槽位名称
 * @param bAutoApplyToSubsystem 是否在落盘成功后直接热应用已校验归档至当前已注册段（零二次读盘）
 * @param TimeoutSeconds 请求超时秒数
 * @param Callback 操作完成回调闭包
 * @param CancellationToken 可选外部协同取消令牌
 */
void UGameCloudSaveSubsystem::DownloadSlotAsync(
    const FString& SlotName,
    bool bAutoApplyToSubsystem,
    float TimeoutSeconds,
    FOnCloudDownloadCompleted Callback,
    FCancellationToken CancellationToken
) {
    TSharedPtr<FCancellationTokenSource> LinkedSource;
    const FCancellationToken EffectiveToken = ResolveEffectiveToken(CancellationToken, LinkedSource);
    if (EffectiveToken.IsCancellationRequested()) {
        GAME_LOG(Log, TEXT("【云存档】下载请求在派发前已被取消: 槽位=%s"), *SlotName);
        NotifyDownloadFailure(SlotName, EGameCloudSaveResult::Cancelled, CloudErrCancelled, TEXT("DownloadCancelled"), Callback);
        return;
    }

    if (!CheckSlotAllowed(this, SlotName)) {
        GAME_LOG(Warning, TEXT("【云存档】下载被拒：槽位名称无效或不在白名单内: %s"), *SlotName);
        NotifyDownloadFailure(SlotName, EGameCloudSaveResult::SlotNotFound, CloudErrInvalidSlot, TEXT("槽位名称无效或不在白名单内"), Callback);
        return;
    }

    UGameNetServiceSubsystem* NetService = UGameNetServiceSubsystem::GetPtr(this);
    if (NetService == nullptr || !NetService->IsConnected()) {
        GAME_LOG(Warning, TEXT("【云存档】下载被拒：网络服务未连接: 槽位=%s"), *SlotName);
        NotifyDownloadFailure(SlotName, EGameCloudSaveResult::NetworkError, CloudErrNetworkDisconnected, TEXT("网络服务未连接"), Callback);
        return;
    }

    FGameCloudSaveDownloadReq Req;
    Req.SlotName = SlotName;

    const uint64 RequestGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);

    TWeakObjectPtr<UGameCloudSaveSubsystem> WeakThis(this);
    NetService->SendStructRequest<FGameCloudSaveDownloadReq, FGameCloudSaveDownloadRsp>(
        NET_MSG_CLOUD_SAVE_DOWNLOAD_REQ,
        NET_MSG_CLOUD_SAVE_DOWNLOAD_RSP,
        Req,
        TimeoutSeconds,
        [WeakThis, SlotName, RequestGeneration, bAutoApplyToSubsystem, EffectiveToken, LinkedSource = MoveTemp(LinkedSource), Callback = MoveTemp(Callback)](
            ENetRequestResult NetResult,
            const FGameCloudSaveDownloadRsp* ResponsePtr
        ) mutable {
            UGameCloudSaveSubsystem* Self = WeakThis.Get();
            if (Self == nullptr) {
                if (Callback) {
                    FGameCloudSaveDownloadRsp CancelledRsp;
                    CancelledRsp.SlotName = SlotName;
                    CancelledRsp.bSuccess = false;
                    CancelledRsp.ErrorCode = CloudErrCancelled;
                    CancelledRsp.ErrorMessage = TEXT("SubsystemDestroyed");
                    Callback(EGameCloudSaveResult::Cancelled, CancelledRsp);
                }
                return;
            }
            Self->HandleDownloadResponse(
                SlotName,
                RequestGeneration,
                bAutoApplyToSubsystem,
                NetResult,
                ResponsePtr,
                MoveTemp(Callback),
                EffectiveToken,
                MoveTemp(LinkedSource)
            );
        }
    );
}

/**
 * @brief 异步查询远端云端指定槽位存档的元信息（包含云端时间戳与大小，支持协同取消）
 * @param SlotName 目标存档槽位名称
 * @param TimeoutSeconds 请求超时秒数
 * @param Callback 查询完成回调闭包
 * @param CancellationToken 可选外部协同取消令牌
 */
void UGameCloudSaveSubsystem::QueryCloudSlotMetadataAsync(
    const FString& SlotName,
    float TimeoutSeconds,
    FOnCloudQueryCompleted Callback,
    FCancellationToken CancellationToken
) {
    TSharedPtr<FCancellationTokenSource> LinkedSource;
    const FCancellationToken EffectiveToken = ResolveEffectiveToken(CancellationToken, LinkedSource);
    auto InvokeQueryFailure = [&SlotName, &Callback](EGameCloudSaveResult Result, int32 ErrorCode, const FString& ErrorMessage) {
        if (Callback) {
            FGameCloudSaveQueryRsp FailedRsp;
            FailedRsp.SlotName = SlotName;
            FailedRsp.ErrorCode = ErrorCode;
            FailedRsp.ErrorMessage = ErrorMessage;
            Callback(Result, FailedRsp);
        }
    };

    if (EffectiveToken.IsCancellationRequested()) {
        GAME_LOG(Log, TEXT("【云存档】元数据查询在派发前已被取消: 槽位=%s"), *SlotName);
        InvokeQueryFailure(EGameCloudSaveResult::Cancelled, CloudErrCancelled, TEXT("QueryCancelled"));
        return;
    }

    if (!CheckSlotAllowed(this, SlotName)) {
        GAME_LOG(Warning, TEXT("【云存档】元数据查询被拒：槽位名称无效或不在白名单内: %s"), *SlotName);
        InvokeQueryFailure(EGameCloudSaveResult::SlotNotFound, CloudErrInvalidSlot, TEXT("槽位名称无效或不在白名单内"));
        return;
    }

    UGameNetServiceSubsystem* NetService = UGameNetServiceSubsystem::GetPtr(this);
    if (NetService == nullptr || !NetService->IsConnected()) {
        GAME_LOG(Warning, TEXT("【云存档】元数据查询被拒：网络服务未连接: 槽位=%s"), *SlotName);
        InvokeQueryFailure(EGameCloudSaveResult::NetworkError, CloudErrNetworkDisconnected, TEXT("网络服务未连接"));
        return;
    }

    FGameCloudSaveQueryReq Req;
    Req.SlotName = SlotName;

    NetService->SendStructRequest<FGameCloudSaveQueryReq, FGameCloudSaveQueryRsp>(
        NET_MSG_CLOUD_SAVE_QUERY_REQ,
        NET_MSG_CLOUD_SAVE_QUERY_RSP,
        Req,
        TimeoutSeconds,
        [SlotName, EffectiveToken, LinkedSource, Callback = MoveTemp(Callback)](
            ENetRequestResult NetResult,
            const FGameCloudSaveQueryRsp* ResponsePtr
        ) {
            if (EffectiveToken.IsCancellationRequested()) {
                GAME_LOG(Log, TEXT("【云存档】元数据查询响应因取消令牌被丢弃: 槽位=%s"), *SlotName);
                if (Callback) {
                    FGameCloudSaveQueryRsp CancelledRsp;
                    CancelledRsp.SlotName = SlotName;
                    CancelledRsp.ErrorCode = CloudErrCancelled;
                    CancelledRsp.ErrorMessage = TEXT("QueryCancelled");
                    Callback(EGameCloudSaveResult::Cancelled, CancelledRsp);
                }
                return;
            }
            if (NetResult != ENetRequestResult::Success || ResponsePtr == nullptr) {
                GAME_LOG(Warning, TEXT("【云存档】元数据查询网络失败: 槽位=%s, NetResult=%d"), *SlotName, static_cast<int32>(NetResult));
                if (Callback) {
                    FGameCloudSaveQueryRsp FailedRsp;
                    FailedRsp.SlotName = SlotName;
                    FailedRsp.ErrorCode = static_cast<int32>(NetResult);
                    FailedRsp.ErrorMessage = TEXT("QueryNetworkFailed");
                    Callback(EGameCloudSaveResult::NetworkError, FailedRsp);
                }
                return;
            }

            EGameCloudSaveResult Result = EGameCloudSaveResult::Success;
            if (!ResponsePtr->bSuccess) {
                Result = (ResponsePtr->ErrorCode == CloudHttpNotFoundCode)
                    ? EGameCloudSaveResult::SlotNotFound
                    : EGameCloudSaveResult::NetworkError;
                GAME_LOG(Warning, TEXT("【云存档】元数据查询服务端返回失败: 槽位=%s, ErrorCode=%d, Msg=%s"),
                    *SlotName, ResponsePtr->ErrorCode, *ResponsePtr->ErrorMessage);
            } else if (!ResponsePtr->bExistsOnCloud) {
                Result = EGameCloudSaveResult::SlotNotFound;
                GAME_LOG(Log, TEXT("【云存档】云端不存在该槽位元数据: 槽位=%s"), *SlotName);
            } else {
                GAME_LOG(Log, TEXT("【云存档】元数据查询成功: 槽位=%s, 大小=%lld 字节"), *SlotName, ResponsePtr->SaveSizeBytes);
            }

            if (Callback) {
                Callback(Result, *ResponsePtr);
            }
        }
    );
}

/**
 * @brief 异步上传指定槽位的本地存档快照至远端（蓝图调用便捷接口，结果触发 OnCloudUploadComplete）
 * @param SlotName 目标存档槽位名称
 * @param TimeoutSeconds 请求超时秒数
 */
void UGameCloudSaveSubsystem::UploadSlot(const FString& SlotName, float TimeoutSeconds) {
    UploadSlotAsync(SlotName, TimeoutSeconds, nullptr);
}

/**
 * @brief 异步从远端拉取下载并安全换入本地（蓝图调用便捷接口，结果触发 OnCloudDownloadComplete）
 * @param SlotName 目标存档槽位名称
 * @param bAutoApplyToSubsystem 是否在落盘后自动应用至当前段
 * @param TimeoutSeconds 请求超时秒数
 */
void UGameCloudSaveSubsystem::DownloadSlot(
    const FString& SlotName,
    bool bAutoApplyToSubsystem,
    float TimeoutSeconds
) {
    DownloadSlotAsync(SlotName, bAutoApplyToSubsystem, TimeoutSeconds, nullptr);
}

/**
 * @brief 通知上传失败并广播委托与调用回调
 * @param SlotName 槽位名称
 * @param Result 结果状态枚举
 * @param ErrorCode 错误码
 * @param ErrorMessage 错误详情
 * @param Callback 完成回调
 */
void UGameCloudSaveSubsystem::NotifyUploadFailure(
    const FString& SlotName,
    EGameCloudSaveResult Result,
    int32 ErrorCode,
    const FString& ErrorMessage,
    const FOnCloudUploadCompleted& Callback
) {
    FGameCloudSaveUploadRsp FailedRsp;
    FailedRsp.SlotName = SlotName;
    FailedRsp.ErrorCode = ErrorCode;
    FailedRsp.ErrorMessage = ErrorMessage;
    NotifyUploadComplete(SlotName, Result, FailedRsp, Callback);
}

/**
 * @brief 通知上传结算并广播委托与调用回调
 * @param SlotName 槽位名称
 * @param Result 结果状态枚举
 * @param Rsp 响应结构体
 * @param Callback 完成回调
 */
void UGameCloudSaveSubsystem::NotifyUploadComplete(
    const FString& SlotName,
    EGameCloudSaveResult Result,
    const FGameCloudSaveUploadRsp& Rsp,
    const FOnCloudUploadCompleted& Callback
) {
    if (!LifetimeCancelSource.IsCancellationRequested()) {
        OnCloudUploadComplete.Broadcast(SlotName, Result, Rsp.ErrorMessage);
    }
    if (Callback) {
        Callback(Result, Rsp);
    }
}

/**
 * @brief 通知下载失败并广播委托与调用回调
 * @param SlotName 槽位名称
 * @param Result 结果状态枚举
 * @param ErrorCode 错误码
 * @param ErrorMessage 错误详情
 * @param Callback 完成回调
 */
void UGameCloudSaveSubsystem::NotifyDownloadFailure(
    const FString& SlotName,
    EGameCloudSaveResult Result,
    int32 ErrorCode,
    const FString& ErrorMessage,
    const FOnCloudDownloadCompleted& Callback
) {
    FGameCloudSaveDownloadRsp FailedRsp;
    FailedRsp.SlotName = SlotName;
    FailedRsp.bSuccess = false;
    FailedRsp.ErrorCode = ErrorCode;
    FailedRsp.ErrorMessage = ErrorMessage;
    NotifyDownloadComplete(SlotName, Result, FailedRsp, Callback);
}

/**
 * @brief 通知下载结算并广播委托与调用回调
 * @param SlotName 槽位名称
 * @param Result 结果状态枚举
 * @param Rsp 响应结构体
 * @param Callback 完成回调
 */
void UGameCloudSaveSubsystem::NotifyDownloadComplete(
    const FString& SlotName,
    EGameCloudSaveResult Result,
    const FGameCloudSaveDownloadRsp& Rsp,
    const FOnCloudDownloadCompleted& Callback
) {
    if (!LifetimeCancelSource.IsCancellationRequested()) {
        OnCloudDownloadComplete.Broadcast(SlotName, Result, Rsp.ErrorMessage);
    }
    if (Callback) {
        Callback(Result, Rsp);
    }
}

/**
 * @brief 在主线程承接本地读取结果并发送云端上传 RPC 请求
 * @param SlotName 槽位名称
 * @param TimeoutSeconds 超时秒数
 * @param ReadResult 后台读取结果
 * @param Callback 完成回调
 * @param EffectiveToken 联合生命周期与外部的取消令牌
 * @param LinkedSource 联合取消源（延长注册生命周期直至 RPC 回调结算）
 */
void UGameCloudSaveSubsystem::DispatchUploadRequest(
    const FString& SlotName,
    float TimeoutSeconds,
    TGameStatusResult<TArray<uint8>> ReadResult,
    FOnCloudUploadCompleted Callback,
    FCancellationToken EffectiveToken,
    TSharedPtr<FCancellationTokenSource> LinkedSource
) {
    if (EffectiveToken.IsCancellationRequested()) {
        GAME_LOG(Log, TEXT("【云存档】上传在发包前被取消: 槽位=%s"), *SlotName);
        NotifyUploadFailure(SlotName, EGameCloudSaveResult::Cancelled, CloudErrCancelled, TEXT("UploadCancelled"), Callback);
        return;
    }

    if (ReadResult.IsFailure()) {
        const int32 ReadErrCode = ReadResult.GetError().ErrorCode;
        if (ReadErrCode == Game::DataErrorCodes::SaveSlotNotFound) {
            GAME_LOG(Warning, TEXT("【云存档】上传失败：本地存档槽位不存在: 槽位=%s"), *SlotName);
            NotifyUploadFailure(SlotName, EGameCloudSaveResult::SlotNotFound, CloudErrLocalSlotMissing, ReadResult.GetError().Message, Callback);
        } else if (ReadErrCode == Game::DataErrorCodes::CorruptedSaveFile) {
            GAME_LOG(Error, TEXT("【云存档】上传失败：本地存档已损坏: 槽位=%s"), *SlotName);
            NotifyUploadFailure(SlotName, EGameCloudSaveResult::CorruptedData, CloudErrCorruptedPayload, ReadResult.GetError().Message, Callback);
        } else {
            GAME_LOG(Error, TEXT("【云存档】上传失败：本地存档读取或恢复 I/O 错误: 槽位=%s"), *SlotName);
            NotifyUploadFailure(SlotName, EGameCloudSaveResult::IoError, CloudErrLocalReadFailed, ReadResult.GetError().Message, Callback);
        }
        return;
    }

    UGameNetServiceSubsystem* NetService = UGameNetServiceSubsystem::GetPtr(this);
    if (NetService == nullptr || !NetService->IsConnected()) {
        GAME_LOG(Warning, TEXT("【云存档】上传失败：读盘后网络已断开: 槽位=%s"), *SlotName);
        NotifyUploadFailure(SlotName, EGameCloudSaveResult::NetworkError, CloudErrNetworkDisconnected, TEXT("网络服务未连接"), Callback);
        return;
    }

    FGameCloudSaveUploadReq Req;
    Req.SlotName = SlotName;
    Req.ArchiveVersion = UGameSaveArchive::CurrentArchiveVersion;
    Req.SaveTimestamp = FDateTime::UtcNow();
    Req.SaveBytes = ReadResult.ExtractValue();

    GAME_LOG(Log, TEXT("【云存档】开始上传槽位快照: 槽位=%s, 字节数=%d"), *SlotName, Req.SaveBytes.Num());

    TWeakObjectPtr<UGameCloudSaveSubsystem> WeakThis(this);
    NetService->SendStructRequest<FGameCloudSaveUploadReq, FGameCloudSaveUploadRsp>(
        NET_MSG_CLOUD_SAVE_UPLOAD_REQ,
        NET_MSG_CLOUD_SAVE_UPLOAD_RSP,
        Req,
        TimeoutSeconds,
        [WeakThis, SlotName, EffectiveToken, LinkedSource = MoveTemp(LinkedSource), Callback = MoveTemp(Callback)](
            ENetRequestResult NetResult,
            const FGameCloudSaveUploadRsp* ResponsePtr
        ) {
            EGameCloudSaveResult FinalResult = EGameCloudSaveResult::NetworkError;
            FGameCloudSaveUploadRsp FinalRsp;
            FinalRsp.SlotName = SlotName;

            if (EffectiveToken.IsCancellationRequested()) {
                FinalResult = EGameCloudSaveResult::Cancelled;
                FinalRsp.bSuccess = false;
                FinalRsp.ErrorCode = CloudErrCancelled;
                FinalRsp.ErrorMessage = TEXT("UploadCancelled");
                GAME_LOG(Log, TEXT("【云存档】上传响应因取消令牌被忽略: 槽位=%s"), *SlotName);
            } else if (NetResult == ENetRequestResult::Success && ResponsePtr != nullptr) {
                FinalRsp = *ResponsePtr;
                FinalResult = MapUploadResponseResult(FinalRsp);
                if (FinalResult == EGameCloudSaveResult::Success) {
                    GAME_LOG(Log, TEXT("【云存档】上传槽位成功: 槽位=%s"), *SlotName);
                } else {
                    GAME_LOG(Warning, TEXT("【云存档】上传服务端返回异常: 槽位=%s, ErrorCode=%d, Msg=%s"),
                        *SlotName, FinalRsp.ErrorCode, *FinalRsp.ErrorMessage);
                }
            } else {
                FinalRsp.bSuccess = false;
                FinalRsp.ErrorCode = static_cast<int32>(NetResult);
                FinalRsp.ErrorMessage = (NetResult == ENetRequestResult::TimedOut)
                    ? TEXT("RequestTimeout")
                    : TEXT("NetworkSendFailed");
                GAME_LOG(Warning, TEXT("【云存档】上传网络异常: 槽位=%s, NetResult=%d"), *SlotName, static_cast<int32>(NetResult));
            }

            if (UGameCloudSaveSubsystem* Subsystem = WeakThis.Get()) {
                Subsystem->NotifyUploadComplete(SlotName, FinalResult, FinalRsp, Callback);
            } else if (Callback) {
                Callback(FinalResult, FinalRsp);
            }
        }
    );
}

/**
 * @brief 在主线程处理云端下载响应，先内存反序列化校验，通过后再派发后台原子落盘
 * @param SlotName 槽位名称
 * @param RequestGeneration 请求派发时签发的槽位写入代次
 * @param bAutoApplyToSubsystem 是否落盘后热应用至本地已注册段
 * @param NetResult 底层网络请求结果
 * @param ResponsePtr 响应结构体指针
 * @param Callback 完成回调
 * @param EffectiveToken 联合生命周期与外部的取消令牌
 * @param LinkedSource 联合取消源（延长注册生命周期直至后台落盘结算）
 */
void UGameCloudSaveSubsystem::HandleDownloadResponse(
    const FString& SlotName,
    uint64 RequestGeneration,
    bool bAutoApplyToSubsystem,
    ENetRequestResult NetResult,
    const FGameCloudSaveDownloadRsp* ResponsePtr,
    FOnCloudDownloadCompleted Callback,
    FCancellationToken EffectiveToken,
    TSharedPtr<FCancellationTokenSource> LinkedSource
) {
    if (EffectiveToken.IsCancellationRequested()) {
        GAME_LOG(Log, TEXT("【云存档】下载响应因取消令牌中止，本地存档未触碰: 槽位=%s"), *SlotName);
        NotifyDownloadFailure(SlotName, EGameCloudSaveResult::Cancelled, CloudErrCancelled, TEXT("DownloadCancelled"), Callback);
        return;
    }

    if (FSaveGameFileIO::IsSlotSaveGenerationSuperseded(SlotName, RequestGeneration)) {
        GAME_LOG(Log, TEXT("【云存档】下载响应已过期（请求在途期间发生了更新保存/下载），本地存档未触碰: 槽位=%s"), *SlotName);
        NotifyDownloadFailure(
            SlotName,
            EGameCloudSaveResult::Cancelled,
            CloudErrSuperseded,
            TEXT("DownloadSupersededByNewerRequestOrLocalSave"),
            Callback
        );
        return;
    }

    if (NetResult != ENetRequestResult::Success || ResponsePtr == nullptr) {
        const FString ErrMsg = (NetResult == ENetRequestResult::TimedOut)
            ? TEXT("DownloadTimeout")
            : TEXT("DownloadNetworkFailed");
        GAME_LOG(Warning, TEXT("【云存档】下载网络失败: 槽位=%s, NetResult=%d"), *SlotName, static_cast<int32>(NetResult));
        NotifyDownloadFailure(SlotName, EGameCloudSaveResult::NetworkError, static_cast<int32>(NetResult), ErrMsg, Callback);
        return;
    }

    FGameCloudSaveDownloadRsp FinalRsp = *ResponsePtr;
    if (!FinalRsp.bSuccess) {
        const EGameCloudSaveResult Result = (FinalRsp.ErrorCode == CloudHttpNotFoundCode)
            ? EGameCloudSaveResult::SlotNotFound
            : EGameCloudSaveResult::NetworkError;
        GAME_LOG(Warning, TEXT("【云存档】下载服务端返回失败: 槽位=%s, ErrorCode=%d, Msg=%s"),
            *SlotName, FinalRsp.ErrorCode, *FinalRsp.ErrorMessage);
        NotifyDownloadComplete(SlotName, Result, FinalRsp, Callback);
        return;
    }

    UGameSaveArchive* ValidatedArchivePtr = nullptr;
    FString ValidationError;
    const EGameCloudSaveResult ValidationResult = ValidateDownloadedBytes(
        SlotName,
        FinalRsp.SaveBytes,
        bAutoApplyToSubsystem,
        ValidatedArchivePtr,
        ValidationError
    );
    if (ValidationResult != EGameCloudSaveResult::Success) {
        FinalRsp.bSuccess = false;
        if (ValidationResult == EGameCloudSaveResult::VersionMismatch) {
            FinalRsp.ErrorCode = CloudErrVersionMismatch;
        } else if (ValidationResult == EGameCloudSaveResult::IoError) {
            FinalRsp.ErrorCode = CloudErrDiskWriteFailed;
        } else {
            FinalRsp.ErrorCode = CloudErrCorruptedPayload;
        }
        FinalRsp.ErrorMessage = ValidationError;
        GAME_LOG(Error, TEXT("【云存档】云端存档落盘前校验失败，已拦截并保护本地存档: 槽位=%s, 原因=%s"), *SlotName, *ValidationError);
        NotifyDownloadComplete(SlotName, ValidationResult, FinalRsp, Callback);
        return;
    }

    const uint64 SaveGeneration = RequestGeneration;
    const uint64 PendingDownloadId = ++NextPendingDownloadId;
    if (bAutoApplyToSubsystem && ValidatedArchivePtr != nullptr) {
        PendingDownloadArchives.Add(PendingDownloadId, ValidatedArchivePtr);
    }

    TWeakObjectPtr<UGameCloudSaveSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(
        FCancellationToken::None(),
        [WeakThis, SlotName, PendingDownloadId, SaveGeneration, bAutoApplyToSubsystem, EffectiveToken, LinkedSource = MoveTemp(LinkedSource), FinalRsp = MoveTemp(FinalRsp), Callback = MoveTemp(Callback)]() mutable {
            bool bSkippedStale = false;
            TGameStatusResult<void> WriteResult = TGameStatusResult<void>::Success();
            if (!EffectiveToken.IsCancellationRequested()) {
                WriteResult = FSaveGameFileIO::WriteSlotBytes(SlotName, FinalRsp.SaveBytes, SaveGeneration, &bSkippedStale);
            }
            FGameThreadTaskRunner::RunOnGameThread(
                [WeakThis, SlotName, PendingDownloadId, SaveGeneration, bAutoApplyToSubsystem, WriteResult = MoveTemp(WriteResult), bSkippedStale, EffectiveToken, LinkedSource = MoveTemp(LinkedSource), FinalRsp = MoveTemp(FinalRsp), Callback = MoveTemp(Callback)]() mutable {
                    UGameCloudSaveSubsystem* Self = WeakThis.Get();
                    if (Self == nullptr) {
                        if (Callback) {
                            FinalRsp.bSuccess = false;
                            FinalRsp.ErrorCode = CloudErrCancelled;
                            FinalRsp.ErrorMessage = TEXT("SubsystemDestroyed");
                            Callback(EGameCloudSaveResult::Cancelled, FinalRsp);
                        }
                        return;
                    }
                    Self->CompleteDownloadAfterDiskWrite(
                        SlotName,
                        PendingDownloadId,
                        SaveGeneration,
                        bAutoApplyToSubsystem,
                        WriteResult,
                        bSkippedStale,
                        MoveTemp(FinalRsp),
                        Callback,
                        EffectiveToken
                    );
                }
            );
        }
    );
}

/**
 * @brief 在主线程结算下载后台原子落盘结果并可选热应用已校验的内存归档
 * @param SlotName 槽位名称
 * @param PendingDownloadId 子系统级全局唯一的下载请求序号（索引 PendingDownloadArchives 防止跨槽位碰撞）
 * @param SaveGeneration 本次落盘签发的槽位级代次序号
 * @param bAutoApplyToSubsystem 是否热应用至已注册段
 * @param WriteResult 后台落盘状态
 * @param bSkippedStale 是否因被更新写入/删槽超越而跳过了物理落盘
 * @param FinalRsp 下载响应副本
 * @param Callback 完成回调
 * @param EffectiveToken 生效取消令牌
 */
void UGameCloudSaveSubsystem::CompleteDownloadAfterDiskWrite(
    const FString& SlotName,
    uint64 PendingDownloadId,
    uint64 SaveGeneration,
    bool bAutoApplyToSubsystem,
    const TGameStatusResult<void>& WriteResult,
    bool bSkippedStale,
    FGameCloudSaveDownloadRsp FinalRsp,
    const FOnCloudDownloadCompleted& Callback,
    const FCancellationToken& EffectiveToken
) {
    TObjectPtr<UGameSaveArchive> ValidatedArchive = nullptr;
    PendingDownloadArchives.RemoveAndCopyValue(PendingDownloadId, ValidatedArchive);

    if (EffectiveToken.IsCancellationRequested()) {
        FinalRsp.bSuccess = false;
        FinalRsp.ErrorCode = CloudErrCancelled;
        FinalRsp.ErrorMessage = TEXT("DownloadCancelled");
        NotifyDownloadComplete(SlotName, EGameCloudSaveResult::Cancelled, FinalRsp, Callback);
        return;
    }
    if (bSkippedStale || FSaveGameFileIO::IsSlotSaveGenerationSuperseded(SlotName, SaveGeneration)) {
        FinalRsp.bSuccess = false;
        FinalRsp.ErrorCode = CloudErrSuperseded;
        FinalRsp.ErrorMessage = TEXT("DownloadSupersededByNewerLocalSave");
        GAME_LOG(Log, TEXT("【云存档】云端下载落盘已被更新的本地存档或删槽超越，跳过热应用: 槽位=%s"), *SlotName);
        NotifyDownloadComplete(SlotName, EGameCloudSaveResult::Cancelled, FinalRsp, Callback);
        return;
    }
    if (WriteResult.IsFailure()) {
        FinalRsp.bSuccess = false;
        FinalRsp.ErrorCode = CloudErrDiskWriteFailed;
        FinalRsp.ErrorMessage = WriteResult.GetError().Message;
        GAME_LOG(Error, TEXT("【云存档】云端存档原子落盘失败: 槽位=%s, 原因=%s"), *SlotName, *FinalRsp.ErrorMessage);
        NotifyDownloadComplete(SlotName, EGameCloudSaveResult::IoError, FinalRsp, Callback);
        return;
    }

    if (bAutoApplyToSubsystem) {
        USaveGameSubsystem* SaveSubsystem = USaveGameSubsystem::GetPtr(this);
        if (SaveSubsystem == nullptr || ValidatedArchive == nullptr) {
            FinalRsp.bSuccess = false;
            FinalRsp.ErrorCode = CloudErrDiskWriteFailed;
            FinalRsp.ErrorMessage = TEXT("SaveSubsystemUnavailable");
            NotifyDownloadComplete(SlotName, EGameCloudSaveResult::IoError, FinalRsp, Callback);
            return;
        }
        const TGameStatusResult<void> ApplyResult = SaveSubsystem->ApplyArchiveToSegments(SlotName, *ValidatedArchive);
        if (ApplyResult.IsFailure()) {
            FinalRsp.bSuccess = false;
            const EGameCloudSaveResult MappedResult = MapArchiveValidationError(ApplyResult.GetError().ErrorCode);
            FinalRsp.ErrorCode = (MappedResult == EGameCloudSaveResult::VersionMismatch)
                ? CloudErrVersionMismatch
                : CloudErrCorruptedPayload;
            FinalRsp.ErrorMessage = ApplyResult.GetError().Message;
            GAME_LOG(Error, TEXT("【云存档】云端存档热应用失败: 槽位=%s, 原因=%s"), *SlotName, *FinalRsp.ErrorMessage);
            NotifyDownloadComplete(SlotName, MappedResult, FinalRsp, Callback);
            return;
        }
    }

    GAME_LOG(Log, TEXT("【云存档】云端存档下载落盘成功: 槽位=%s, 字节数=%d, 自动热应用=%s"),
        *SlotName, FinalRsp.SaveBytes.Num(), bAutoApplyToSubsystem ? TEXT("True") : TEXT("False"));
    NotifyDownloadComplete(SlotName, EGameCloudSaveResult::Success, FinalRsp, Callback);
}

/**
 * @brief 在主线程落盘前反序列化并验证云端存档字节流完整性与版本兼容性（零磁盘污染）
 * @param SlotName 槽位名称
 * @param SaveBytes 云端返回的存档字节流
 * @param bAutoApplyToSubsystem 是否需要返回可热应用的 UGameSaveArchive
 * @param OutValidatedArchive 校验通过时输出内存归档对象（仅当 bAutoApplyToSubsystem 为 true 时填充）
 * @param OutErrorMessage 校验失败时输出错误说明
 * @return 校验结果状态（Success / CorruptedData / VersionMismatch / IoError）
 */
EGameCloudSaveResult UGameCloudSaveSubsystem::ValidateDownloadedBytes(
    const FString& SlotName,
    const TArray<uint8>& SaveBytes,
    bool bAutoApplyToSubsystem,
    UGameSaveArchive*& OutValidatedArchive,
    FString& OutErrorMessage
) const {
    OutValidatedArchive = nullptr;
    if (!FSaveGameFileIO::HasValidSaveGameHeader(SaveBytes, SlotName)) {
        OutErrorMessage = TEXT("CorruptedCloudPayload");
        return EGameCloudSaveResult::CorruptedData;
    }

    const USaveGameSubsystem* SaveSubsystem = USaveGameSubsystem::GetPtr(this);
    if (SaveSubsystem != nullptr) {
        const TGameStatusResult<UGameSaveArchive*> ValidateResult = SaveSubsystem->DeserializeAndValidateArchive(SlotName, SaveBytes);
        if (ValidateResult.IsFailure()) {
            OutErrorMessage = ValidateResult.GetError().Message;
            return MapArchiveValidationError(ValidateResult.GetError().ErrorCode);
        }
        if (bAutoApplyToSubsystem) {
            OutValidatedArchive = ValidateResult.GetValue();
        }
        return EGameCloudSaveResult::Success;
    }

    if (bAutoApplyToSubsystem) {
        OutErrorMessage = TEXT("SaveSubsystemUnavailable");
        return EGameCloudSaveResult::IoError;
    }

    USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(SaveBytes);
    if (LoadedObject == nullptr) {
        OutErrorMessage = TEXT("CorruptedCloudPayload");
        return EGameCloudSaveResult::CorruptedData;
    }
    if (const UGameSaveArchive* Archive = Cast<UGameSaveArchive>(LoadedObject)) {
        if (Archive->Metadata.ArchiveVersion <= 0 || Archive->Metadata.ArchiveVersion > UGameSaveArchive::CurrentArchiveVersion) {
            OutErrorMessage = FString::Printf(
                TEXT("归档版本 %d 非法或高于当前支持的版本 %d"),
                Archive->Metadata.ArchiveVersion,
                UGameSaveArchive::CurrentArchiveVersion
            );
            return EGameCloudSaveResult::VersionMismatch;
        }
    }
    return EGameCloudSaveResult::Success;
}

/**
 * @brief 构造结合子系统生命周期取消源与调用方取消令牌的联合取消上下文
 * @param ExternalToken 外部传入的可选取消令牌
 * @param OutLinkedSource 联合取消源输出（仅在外部令牌可取消时创建以延长生命周期）
 * @return 生效的取消令牌
 */
FCancellationToken UGameCloudSaveSubsystem::ResolveEffectiveToken(
    const FCancellationToken& ExternalToken,
    TSharedPtr<FCancellationTokenSource>& OutLinkedSource
) const {
    const FCancellationToken LifetimeToken = LifetimeCancelSource.GetToken();
    if (!ExternalToken.CanBeCancelled()) {
        return LifetimeToken;
    }
    OutLinkedSource = FCancellationTokenSource::CreateLinkedTokenSource(LifetimeToken, ExternalToken);
    return OutLinkedSource->GetToken();
}
