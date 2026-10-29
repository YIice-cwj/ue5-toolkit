/**
 * @file GameSaveTypes.h
 * @brief 通用存档分段类型契约定义（单一职责 · 结果码、元数据与分段负载）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

#include "GameSaveTypes.generated.h"

class FArchive;

/**
 * @brief 存档操作执行结果码
 */
UENUM(BlueprintType)
enum class ESaveGameResult : uint8 {
    /**
     * @brief 操作成功
     */
    Success,

    /**
     * @brief 底层文件读写或 I/O 操作失败
     */
    IoError,

    /**
     * @brief 数据损坏或反序列化异常
     */
    CorruptedData,

    /**
     * @brief 归档或分段版本号不兼容
     */
    VersionMismatch,

    /**
     * @brief 找不到指定的存档槽位文件
     */
    SlotNotFound,
};

/**
 * @brief 存档槽位元信息（与具体业务域无关）
 */
USTRUCT(BlueprintType)
struct GAMEDATACORE_API FGameSaveSlotMetadata {
    GENERATED_BODY()

    /**
     * @brief 存档槽位名称
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FString SlotName;

    /**
     * @brief 保存完成时的绝对真实时间戳
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FDateTime SaveTimestamp;

    /**
     * @brief 归档格式主版本号
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    int32 ArchiveVersion = 1;
};

/**
 * @brief 单个存档段的落盘二进制负载（段版本 + 段原始字节）
 */
USTRUCT(BlueprintType)
struct GAMEDATACORE_API FGameSaveSegmentPayload {
    GENERATED_BODY()

    /**
     * @brief 该段落盘时的段定义版本号
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    int32 SegmentVersion = 0;

    /**
     * @brief 该段序列化后的二进制字节数组
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    TArray<uint8> Bytes;
};

/**
 * @brief 存档段契约：由业务方注册「段名 + 段版本 + 保存/加载委托」
 *
 * 保存：Save 委托向归档写入本段数据，子系统负责装在段负载内并整体落盘；
 * 加载：Load 委托从归档读取本段数据，StoredVersion 为落盘时的段版本，供业务方自行迁移旧格式；
 * 未知段：未注册的段在读取时原样保留，保存时写回，实现老版本客户端/升级场景下的未知段不丢失。
 */
struct GAMEDATACORE_API FGameSaveSegment {
    /**
     * @brief 存档段唯一标识名称
     */
    FName SegmentName;

    /**
     * @brief 存档段当前版本号（落盘时记录）
     */
    int32 SegmentVersion = 1;

    /**
     * @brief 保存委托：向归档写入本段数据
     */
    TFunction<void(FArchive&)> Save;

    /**
     * @brief 加载委托：从归档读取本段数据并支持旧版本数据迁移
     */
    TFunction<bool(FArchive&, int32 StoredVersion)> Load;
};

