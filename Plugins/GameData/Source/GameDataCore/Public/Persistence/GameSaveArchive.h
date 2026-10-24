/**
 * @file GameSaveArchive.h
 * @brief 通用存档分段归档结构与契约定义
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "GameSaveArchive.generated.h"

class FArchive;

/** 存档操作错误码 */
UENUM(BlueprintType)
enum class ESaveGameResult : uint8 {
    Success,
    IoError,
    CorruptedData,
    VersionMismatch,
    SlotNotFound,
};

/**
 * @brief 存档槽位元信息（与业务域无关）
 */
USTRUCT(BlueprintType)
struct GAMEDATACORE_API FGameSaveSlotMetadata {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FString SlotName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FDateTime SaveTimestamp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    int32 ArchiveVersion = 1;
};

/**
 * @brief 单个存档段的落盘负载（段版本 + 段字节）
 */
USTRUCT(BlueprintType)
struct GAMEDATACORE_API FGameSaveSegmentPayload {
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    int32 SegmentVersion = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    TArray<uint8> Bytes;
};

/**
 * @brief 存档段契约：由业务方注册「段名 + 段版本 + 保存/加载委托」
 *
 * 保存：Save 委托向归档写入本段数据，子系统负责装在段负载内并整体落盘；
 * 加载：Load 委托从归档读取本段数据，StoredVersion 为落盘时的段版本，供业务方自行迁移旧格式；
 * 版本：落盘段版本高于当前注册段版本时整体加载拒绝（拒绝读取未来版本，避免误读）。
 */
struct GAMEDATACORE_API FGameSaveSegment {
    FName SegmentName;
    int32 SegmentVersion = 1;
    TFunction<void(FArchive&)> Save;
    TFunction<bool(FArchive&, int32 StoredVersion)> Load;
};

/**
 * @brief 通用存档归档对象（按段装载业务数据，业务字段不进入通用层）
 *
 * 开发者可派生本类并追加 UPROPERTY 字段，再通过
 * USaveGameSubsystem::SetSaveGameClass 指定派生类型，实现自定义扩展字段。
 */
UCLASS(BlueprintType)
class GAMEDATACORE_API UGameSaveArchive : public USaveGame {
    GENERATED_BODY()

public:
    static constexpr int32 CurrentArchiveVersion = 1;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FGameSaveSlotMetadata Metadata;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    TMap<FName, FGameSaveSegmentPayload> Segments;
};
