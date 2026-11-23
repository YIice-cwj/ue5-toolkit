// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameCloudSaveSubsystem.h
 * @brief 云端存档同步门面子系统声明（先验后落盘防本地破坏 · 协同取消 · GameLogging 全链路观测）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Async/FCancellationTokenSource.h"
#include "Cloud/GameCloudSaveTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/TGameResult.h"

#include "GameCloudSaveSubsystem.generated.h"

class UGameSaveArchive;
enum class ENetRequestResult : uint8;

/**
 * @brief 云存档异步上传完成动态多播委托
 * @param SlotName 存档槽位名称
 * @param Result 同步结果状态枚举
 * @param ErrorMessage 错误详情文本（成功时通常为空）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnCloudSaveUploadDelegate,
    const FString&, SlotName,
    EGameCloudSaveResult, Result,
    const FString&, ErrorMessage
);

/**
 * @brief 云存档异步下载完成动态多播委托
 * @param SlotName 存档槽位名称
 * @param Result 同步结果状态枚举
 * @param ErrorMessage 错误详情文本（成功时通常为空）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnCloudSaveDownloadDelegate,
    const FString&, SlotName,
    EGameCloudSaveResult, Result,
    const FString&, ErrorMessage
);

/**
 * @brief 云端存档同步门面子系统（基于 UGameNetServiceSubsystem 强类型 RPC 通道与先验后落盘保护）
 */
UCLASS(BlueprintType)
class GAMEDATA_API UGameCloudSaveSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    using FOnCloudUploadCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveUploadRsp&)>;
    using FOnCloudDownloadCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveDownloadRsp&)>;
    using FOnCloudQueryCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveQueryRsp&)>;

    UGameCloudSaveSubsystem() = default;
    ~UGameCloudSaveSubsystem() override = default;

    /**
     * @brief 子系统初始化生命周期回调：重置生命周期取消源
     * @param Collection 子系统依赖集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统销毁生命周期回调：取消所有在途云端同步任务并清理待热应用归档缓存
     */
    void Deinitialize() override;

    /**
     * @brief 安全获取云存档子系统指针（支持 UWorld、UGameInstance、UGameInstanceSubsystem 与 AActor）
     * @param WorldContextObject 世界或子系统上下文对象
     * @return 子系统实例指针，未就绪时返回 nullptr
     */
    [[nodiscard]] static UGameCloudSaveSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 获取云存档子系统引用（断言有效）
     * @param WorldContextObject 世界或子系统上下文对象
     * @return 子系统强引用
     */
    [[nodiscard]] static UGameCloudSaveSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 异步上传指定槽位的本地存档快照至远端云存储服务（C++ 强类型回调版本，支持协同取消）
     * @param SlotName 目标存档槽位名称
     * @param TimeoutSeconds 请求超时秒数
     * @param Callback 操作完成回调闭包
     * @param CancellationToken 可选外部协同取消令牌
     */
    void UploadSlotAsync(
        const FString& SlotName,
        float TimeoutSeconds,
        FOnCloudUploadCompleted Callback,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 异步从云端拉取指定槽位存档、先内存反序列化与版本校验，通过后再原子写入本地磁盘并可选热应用
     * @param SlotName 目标存档槽位名称
     * @param bAutoApplyToSubsystem 是否在落盘成功后直接热应用已校验归档至当前已注册段（零二次读盘）
     * @param TimeoutSeconds 请求超时秒数
     * @param Callback 操作完成回调闭包
     * @param CancellationToken 可选外部协同取消令牌
     */
    void DownloadSlotAsync(
        const FString& SlotName,
        bool bAutoApplyToSubsystem,
        float TimeoutSeconds,
        FOnCloudDownloadCompleted Callback,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 异步查询远端云端指定槽位存档的元信息（包含云端时间戳与大小，支持协同取消）
     * @param SlotName 目标存档槽位名称
     * @param TimeoutSeconds 请求超时秒数
     * @param Callback 查询完成回调闭包
     * @param CancellationToken 可选外部协同取消令牌
     */
    void QueryCloudSlotMetadataAsync(
        const FString& SlotName,
        float TimeoutSeconds,
        FOnCloudQueryCompleted Callback,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 异步上传指定槽位的本地存档快照至远端（蓝图调用便捷接口，结果触发 OnCloudUploadComplete）
     * @param SlotName 目标存档槽位名称
     * @param TimeoutSeconds 请求超时秒数
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|CloudSave")
    void UploadSlot(const FString& SlotName, float TimeoutSeconds = 10.0f);

    /**
     * @brief 异步从远端拉取下载并安全换入本地（蓝图调用便捷接口，结果触发 OnCloudDownloadComplete）
     * @param SlotName 目标存档槽位名称
     * @param bAutoApplyToSubsystem 是否在落盘后自动应用至当前段
     * @param TimeoutSeconds 请求超时秒数
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|CloudSave")
    void DownloadSlot(const FString& SlotName, bool bAutoApplyToSubsystem = true, float TimeoutSeconds = 10.0f);

    /**
     * @brief 异步上传完成事件多播广播
     */
    UPROPERTY(BlueprintAssignable, Category = "GameData|CloudSave")
    FOnCloudSaveUploadDelegate OnCloudUploadComplete;

    /**
     * @brief 异步下载完成事件多播广播
     */
    UPROPERTY(BlueprintAssignable, Category = "GameData|CloudSave")
    FOnCloudSaveDownloadDelegate OnCloudDownloadComplete;

private:
    /**
     * @brief 通知上传失败并广播委托与调用回调
     * @param SlotName 槽位名称
     * @param Result 结果状态枚举
     * @param ErrorCode 错误码
     * @param ErrorMessage 错误详情
     * @param Callback 完成回调
     */
    void NotifyUploadFailure(
        const FString& SlotName,
        EGameCloudSaveResult Result,
        int32 ErrorCode,
        const FString& ErrorMessage,
        const FOnCloudUploadCompleted& Callback
    );

    /**
     * @brief 通知上传结算并广播委托与调用回调
     * @param SlotName 槽位名称
     * @param Result 结果状态枚举
     * @param Rsp 响应结构体
     * @param Callback 完成回调
     */
    void NotifyUploadComplete(
        const FString& SlotName,
        EGameCloudSaveResult Result,
        const FGameCloudSaveUploadRsp& Rsp,
        const FOnCloudUploadCompleted& Callback
    );

    /**
     * @brief 通知下载失败并广播委托与调用回调
     * @param SlotName 槽位名称
     * @param Result 结果状态枚举
     * @param ErrorCode 错误码
     * @param ErrorMessage 错误详情
     * @param Callback 完成回调
     */
    void NotifyDownloadFailure(
        const FString& SlotName,
        EGameCloudSaveResult Result,
        int32 ErrorCode,
        const FString& ErrorMessage,
        const FOnCloudDownloadCompleted& Callback
    );

    /**
     * @brief 通知下载结算并广播委托与调用回调
     * @param SlotName 槽位名称
     * @param Result 结果状态枚举
     * @param Rsp 响应结构体
     * @param Callback 完成回调
     */
    void NotifyDownloadComplete(
        const FString& SlotName,
        EGameCloudSaveResult Result,
        const FGameCloudSaveDownloadRsp& Rsp,
        const FOnCloudDownloadCompleted& Callback
    );

    /**
     * @brief 在主线程承接本地读取结果并发送云端上传 RPC 请求
     * @param SlotName 槽位名称
     * @param TimeoutSeconds 超时秒数
     * @param ReadResult 后台读取结果
     * @param Callback 完成回调
     * @param EffectiveToken 联合生命周期与外部的取消令牌
     * @param LinkedSource 联合取消源（延长注册生命周期直至 RPC 回调结算）
     */
    void DispatchUploadRequest(
        const FString& SlotName,
        float TimeoutSeconds,
        TGameStatusResult<TArray<uint8>> ReadResult,
        FOnCloudUploadCompleted Callback,
        FCancellationToken EffectiveToken,
        TSharedPtr<FCancellationTokenSource> LinkedSource
    );

    /**
     * @brief 在主线程处理云端下载响应，先内存反序列化校验，通过后再派发后台原子落盘
     * @param SlotName 槽位名称
     * @param bAutoApplyToSubsystem 是否落盘后热应用至本地已注册段
     * @param NetResult 底层网络请求结果
     * @param ResponsePtr 响应结构体指针
     * @param Callback 完成回调
     * @param EffectiveToken 联合生命周期与外部的取消令牌
     * @param LinkedSource 联合取消源（延长注册生命周期直至后台落盘结算）
     */
    void HandleDownloadResponse(
        const FString& SlotName,
        bool bAutoApplyToSubsystem,
        ENetRequestResult NetResult,
        const FGameCloudSaveDownloadRsp* ResponsePtr,
        FOnCloudDownloadCompleted Callback,
        FCancellationToken EffectiveToken,
        TSharedPtr<FCancellationTokenSource> LinkedSource
    );

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
    void CompleteDownloadAfterDiskWrite(
        const FString& SlotName,
        uint64 PendingDownloadId,
        uint64 SaveGeneration,
        bool bAutoApplyToSubsystem,
        const TGameStatusResult<void>& WriteResult,
        bool bSkippedStale,
        FGameCloudSaveDownloadRsp FinalRsp,
        const FOnCloudDownloadCompleted& Callback,
        const FCancellationToken& EffectiveToken
    );

    /**
     * @brief 在主线程落盘前反序列化并验证云端存档字节流完整性与版本兼容性（零磁盘污染）
     * @param SlotName 槽位名称
     * @param SaveBytes 云端返回的存档字节流
     * @param bAutoApplyToSubsystem 是否需要返回可热应用的 UGameSaveArchive
     * @param OutValidatedArchive 校验通过时输出内存归档对象（仅当 bAutoApplyToSubsystem 为 true 时填充）
     * @param OutErrorMessage 校验失败时输出错误说明
     * @return 校验结果状态（Success / CorruptedData / VersionMismatch / IoError）
     */
    [[nodiscard]] EGameCloudSaveResult ValidateDownloadedBytes(
        const FString& SlotName,
        const TArray<uint8>& SaveBytes,
        bool bAutoApplyToSubsystem,
        UGameSaveArchive*& OutValidatedArchive,
        FString& OutErrorMessage
    ) const;

    /**
     * @brief 构造结合子系统生命周期取消源与调用方取消令牌的联合取消上下文
     * @param ExternalToken 外部传入的可选取消令牌
     * @param OutLinkedSource 联合取消源输出（仅在外部令牌可取消时创建以延长生命周期）
     * @return 生效的取消令牌
     */
    [[nodiscard]] FCancellationToken ResolveEffectiveToken(
        const FCancellationToken& ExternalToken,
        TSharedPtr<FCancellationTokenSource>& OutLinkedSource
    ) const;

    UPROPERTY(Transient)
    TMap<uint64, TObjectPtr<UGameSaveArchive>> PendingDownloadArchives;

    uint64 NextPendingDownloadId{0};

    FCancellationTokenSource LifetimeCancelSource;
};
