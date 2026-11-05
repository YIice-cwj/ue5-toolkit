// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cloud/GameCloudSaveTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "GameCloudSaveSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnCloudSaveUploadDelegate,
    const FString&, SlotName,
    EGameCloudSaveResult, Result,
    const FString&, ErrorMessage
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnCloudSaveDownloadDelegate,
    const FString&, SlotName,
    EGameCloudSaveResult, Result,
    const FString&, ErrorMessage
);

/**
 * @brief 云端存档同步门面子系统（基于 UGameNetServiceSubsystem 强类型 RPC 通道与原子安全落盘）
 */
UCLASS(BlueprintType)
class GAMEDATA_API UGameCloudSaveSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    using FOnCloudUploadCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveUploadRsp&)>;
    using FOnCloudDownloadCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveDownloadRsp&)>;
    using FOnCloudQueryCompleted = TFunction<void(EGameCloudSaveResult, const FGameCloudSaveQueryRsp&)>;

public:
    UGameCloudSaveSubsystem() = default;
    ~UGameCloudSaveSubsystem() override = default;

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

    /**
     * @brief 异步上传指定槽位的本地存档快照至远端云存储服务（C++ 强类型回调版本）
     * @param SlotName 目标存档槽位名称
     * @param TimeoutSeconds 请求超时秒数
     * @param Callback 操作完成回调闭包
     */
    void UploadSlotAsync(
        const FString& SlotName,
        float TimeoutSeconds,
        FOnCloudUploadCompleted Callback
    );

    /**
     * @brief 异步从云端拉取指定槽位存档、执行合法性校验并原子写入本地磁盘（C++ 强类型回调版本）
     * @param SlotName 目标存档槽位名称
     * @param bAutoApplyToSubsystem 是否在落地成功后自动调用 USaveGameSubsystem 反解应用至当前已注册段
     * @param TimeoutSeconds 请求超时秒数
     * @param Callback 操作完成回调闭包
     */
    void DownloadSlotAsync(
        const FString& SlotName,
        bool bAutoApplyToSubsystem,
        float TimeoutSeconds,
        FOnCloudDownloadCompleted Callback
    );

    /**
     * @brief 异步查询远端云端指定槽位存档的元信息（包含云端时间戳与大小）
     * @param SlotName 目标存档槽位名称
     * @param TimeoutSeconds 请求超时秒数
     * @param Callback 查询完成回调闭包
     */
    void QueryCloudSlotMetadataAsync(
        const FString& SlotName,
        float TimeoutSeconds,
        FOnCloudQueryCompleted Callback
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
     * @brief 安全获取云存档子系统指针（可空）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统实例指针，未就绪时返回 nullptr
     */
    [[nodiscard]] static UGameCloudSaveSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 获取云存档子系统引用（断言有效）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统引用
     */
    [[nodiscard]] static UGameCloudSaveSubsystem& Get(const UObject* WorldContextObject);

private:
    /**
     * @brief 将下载落盘成功的存档安全热应用至本地已注册内存段
     * @param SlotName 目标存档槽位名称
     */
    void ApplySlotToSubsystem(const FString& SlotName) const;
};

