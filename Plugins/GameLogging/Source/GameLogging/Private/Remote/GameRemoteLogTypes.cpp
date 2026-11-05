// Copyright Epic Games, Inc. All Rights Reserved.

#include "Remote/GameRemoteLogTypes.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_Game_Log_Remote, "Game.Log.Remote");

/**
 * @brief 带参全字段构造函数实现
 * @param InCategory 日志分类名称
 * @param InVerbosity 日志详细级别
 * @param InMessage 日志正文内容
 * @param InTimestamp 日志发生时钟秒数
 * @param InFrameNumber 日志发生时的渲染/引擎帧序号
 */
FGameRemoteLogEntry::FGameRemoteLogEntry(
    FName InCategory,
    uint8 InVerbosity,
    FString InMessage,
    double InTimestamp,
    int64 InFrameNumber
)
    : Category(InCategory)
    , Verbosity(InVerbosity)
    , Message(MoveTemp(InMessage))
    , Timestamp(InTimestamp)
    , FrameNumber(InFrameNumber) {
}

/**
 * @brief 获取强类型日志详细级别枚举值实现
 * @return 详细级别枚举值
 */
ELogVerbosity::Type FGameRemoteLogEntry::GetVerbosity() const noexcept {
    return static_cast<ELogVerbosity::Type>(Verbosity);
}

/**
 * @brief 相等性比较操作符实现
 * @param Other 待比较的另一日志条目
 * @return 所有关键字段均一致时返回 true
 */
bool FGameRemoteLogEntry::operator==(const FGameRemoteLogEntry& Other) const noexcept {
    return Category == Other.Category &&
           Verbosity == Other.Verbosity &&
           Message == Other.Message &&
           FMath::IsNearlyEqual(Timestamp, Other.Timestamp) &&
           FrameNumber == Other.FrameNumber;
}
