// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameCloudSaveTypes.h
 * @brief 云端存档同步状态枚举与强类型 RPC 请求/响应结构体定义
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

#include "GameCloudSaveTypes.generated.h"

/**
 * @brief 云存档同步结果状态枚举
 */
UENUM(BlueprintType)
enum class EGameCloudSaveResult : uint8 {
    /**
     * @brief 成功完成操作
     */
    Success,

    /**
     * @brief 网络连接未就绪、断线、服务端故障或请求超时
     */
    NetworkError,

    /**
     * @brief 指定槽位在本地或云端不存在，或槽位名不合法
     */
    SlotNotFound,

    /**
     * @brief 归档数据损坏、头部校验或反序列化不通过
     */
    CorruptedData,

    /**
     * @brief 归档或分段版本高于当前运行库支持的版本
     */
    VersionMismatch,

    /**
     * @brief 操作被上层取消令牌或子系统销毁主动取消
     */
    Cancelled,

    /**
     * @brief 本地磁盘原子写入失败
     */
    IoError
};

/**
 * @brief 云存档上传请求载荷
 */
USTRUCT(BlueprintType)
struct GAMEDATA_API FGameCloudSaveUploadReq {
    GENERATED_BODY()

public:
    /**
     * @brief 目标存档槽位名称
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;

    /**
     * @brief 归档格式主版本号
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ArchiveVersion{1};

    /**
     * @brief 本地存档生成时间戳（UTC）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FDateTime SaveTimestamp;

    /**
     * @brief 存档完整二进制字节流
     */
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
    /**
     * @brief 服务端是否成功接收并落盘
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    bool bSuccess{false};

    /**
     * @brief 关联存档槽位名称
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;

    /**
     * @brief 服务端或本地错误码（0 表示成功）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ErrorCode{0};

    /**
     * @brief 错误详情描述
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString ErrorMessage;

    /**
     * @brief 云端确认存档时间戳（UTC）
     */
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
    /**
     * @brief 目标拉取的存档槽位名称
     */
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
    /**
     * @brief 下载及本地校验落盘是否成功
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    bool bSuccess{false};

    /**
     * @brief 关联存档槽位名称
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;

    /**
     * @brief 服务端或本地校验错误码（0 表示成功）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ErrorCode{0};

    /**
     * @brief 错误详情描述
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString ErrorMessage;

    /**
     * @brief 云端存档归档格式版本号
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ArchiveVersion{1};

    /**
     * @brief 云端存档最后修改时间戳（UTC）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FDateTime CloudTimestamp;

    /**
     * @brief 云端返回的存档完整二进制字节流
     */
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
    /**
     * @brief 待查询的存档槽位名称
     */
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
    /**
     * @brief 元信息查询请求是否成功执行
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    bool bSuccess{false};

    /**
     * @brief 关联存档槽位名称
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString SlotName;

    /**
     * @brief 云端是否存在该槽位的存档记录
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    bool bExistsOnCloud{false};

    /**
     * @brief 服务端或网络错误码（0 表示成功）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ErrorCode{0};

    /**
     * @brief 错误详情描述
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FString ErrorMessage;

    /**
     * @brief 云端存档归档格式版本号
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int32 ArchiveVersion{0};

    /**
     * @brief 云端存档最后修改时间戳（UTC）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    FDateTime CloudTimestamp;

    /**
     * @brief 云端存档字节大小
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|CloudSave")
    int64 SaveSizeBytes{0};
};
