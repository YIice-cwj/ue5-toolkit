// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogVerbosity.h"
#include "NativeGameplayTags.h"

#include "GameRemoteLogTypes.generated.h"

GAMELOGGING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Game_Log_Remote);

/**
 * @brief 远程日志解耦数据契约载荷（通过 GameplayMessageSubsystem 广播互通）
 */
USTRUCT(BlueprintType)
struct GAMELOGGING_API FGameRemoteLogEntry {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    FName Category;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    uint8 Verbosity{static_cast<uint8>(ELogVerbosity::Log)};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    FString Message;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    double Timestamp{0.0};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
    int64 FrameNumber{0};

public:
    /**
     * @brief 默认构造函数
     */
    FGameRemoteLogEntry() = default;

    /**
     * @brief 带参全字段构造函数
     * @param InCategory 日志分类名称
     * @param InVerbosity 日志详细级别
     * @param InMessage 日志正文内容
     * @param InTimestamp 日志发生时钟秒数
     * @param InFrameNumber 日志发生时的渲染/引擎帧序号
     */
    FGameRemoteLogEntry(
        FName InCategory,
        uint8 InVerbosity,
        FString InMessage,
        double InTimestamp,
        int64 InFrameNumber
    );

    /**
     * @brief 获取强类型日志详细级别枚举值
     * @return 详细级别枚举值
     */
    [[nodiscard]] ELogVerbosity::Type GetVerbosity() const noexcept;

    /**
     * @brief 相等性比较操作符
     * @param Other 待比较的另一日志条目
     * @return 所有关键字段均一致时返回 true
     */
    [[nodiscard]] bool operator==(const FGameRemoteLogEntry& Other) const noexcept;
};
