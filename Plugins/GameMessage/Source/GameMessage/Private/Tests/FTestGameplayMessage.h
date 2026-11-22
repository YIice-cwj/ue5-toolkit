/**
 * @file FTestGameplayMessage.h
 * @brief GameMessage 自动化测试专用反射载荷结构体集合（基础载荷、继承多态载荷与 UObject 引用保活载荷）
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "FTestGameplayMessage.generated.h"

/**
 * @brief 消息总线基础测试载荷结构体
 */
USTRUCT()
struct FTestGameplayMessage {
    GENERATED_BODY()

    UPROPERTY()
    int32 Value = 0;

    UPROPERTY()
    FString Text;
};

/**
 * @brief 消息总线派生测试载荷结构体（用于验证 UScriptStruct::IsChildOf 继承多态分发与提取）
 */
USTRUCT()
struct FDerivedTestGameplayMessage : public FTestGameplayMessage {
    GENERATED_BODY()

    UPROPERTY()
    int32 ExtraCode = 0;
};

/**
 * @brief 消息总线不相关测试载荷结构体（用于验证类型不匹配过滤与 GetPayload 拒绝提取）
 */
USTRUCT()
struct FUnrelatedTestGameplayMessage {
    GENERATED_BODY()

    UPROPERTY()
    double Metric = 0.0;
};

/**
 * @brief 携带 UObject 强引用的测试载荷结构体（用于验证 UAsyncAction_ListenForGameplayMessage 的 GC 追踪保活）
 */
USTRUCT()
struct FObjectPayloadTestMessage {
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UObject> HeldObject = nullptr;

    UPROPERTY()
    int32 TagId = 0;
};
