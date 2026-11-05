// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

#include "GameCloudSaveTypes.generated.h"

/**
 * @brief 云存档同步结果状态枚举
 */
UENUM(BlueprintType)
enum class EGameCloudSaveResult : uint8 {
    /** 成功完成操作 */
    Success,
    /** 网络连接未就绪、断线或请求超时 */
    NetworkError,
    /** 指定槽位在本地或云端不存在 */
    SlotNotFound,
    /** 归档数据损坏或头部校验不通过 */
    CorruptedData,
    /** 归档版本不兼容 */
    VersionMismatch,
    /** 操作被上层主动取消 */
    Cancelled
};

/**
 * @brief 云存档上传请求载荷
 */
USTRUCT(BlueprintType)
struct GAMEDATA_API FGameCloudSaveUploadReq {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ArchiveVersion{1};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FDateTime SaveTimestamp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    TArray<uint8> SaveBytes;
};

/**
 * @brief 云存档上传响应载荷
 */
USTRUCT(BlueprintType)
struct GAMEDATA_API FGameCloudSaveUploadRsp {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    bool bSuccess{false};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ErrorCode{0};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString ErrorMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FDateTime CloudTimestamp;
};

/**
 * @brief 云存档拉取下载请求载荷
 */
USTRUCT(BlueprintType)
struct GAMEDATA_API FGameCloudSaveDownloadReq {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;
};

/**
 * @brief 云存档拉取下载响应载荷
 */
USTRUCT(BlueprintType)
struct GAMEDATA_API FGameCloudSaveDownloadRsp {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    bool bSuccess{false};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ErrorCode{0};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString ErrorMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ArchiveVersion{1};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FDateTime CloudTimestamp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    TArray<uint8> SaveBytes;
};

/**
 * @brief 云端存档元信息查询请求载荷
 */
USTRUCT(BlueprintType)
struct GAMEDATA_API FGameCloudSaveQueryReq {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;
};

/**
 * @brief 云端存档元信息查询响应载荷
 */
USTRUCT(BlueprintType)
struct GAMEDATA_API FGameCloudSaveQueryRsp {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    bool bSuccess{false};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    bool bExistsOnCloud{false};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ArchiveVersion{0};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FDateTime CloudTimestamp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int64 SaveSizeBytes{0};
};

