/**
 * @file GameCloudSaveTestTypes.h
 * @brief 云存档子系统自动化测试专用动态委托监听器
 */

#pragma once

#include "CoreMinimal.h"
#include "Cloud/GameCloudSaveTypes.h"

#include "GameCloudSaveTestTypes.generated.h"

/**
 * @brief 云存档动态多播委托测试监听器（验证蓝图封装与多播广播契约）
 */
UCLASS()
class GAMEDATA_API UGameCloudSaveTestListener : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 接收云存档上传完成广播
     * @param SlotName 槽位名称
     * @param Result 上传结果状态
     * @param ErrorMessage 错误说明
     */
    UFUNCTION()
    void HandleUploadComplete(const FString& SlotName, EGameCloudSaveResult Result, const FString& ErrorMessage);

    /**
     * @brief 接收云存档下载完成广播
     * @param SlotName 槽位名称
     * @param Result 下载结果状态
     * @param ErrorMessage 错误说明
     */
    UFUNCTION()
    void HandleDownloadComplete(const FString& SlotName, EGameCloudSaveResult Result, const FString& ErrorMessage);

    TArray<EGameCloudSaveResult> UploadResults;
    TArray<EGameCloudSaveResult> DownloadResults;
};
