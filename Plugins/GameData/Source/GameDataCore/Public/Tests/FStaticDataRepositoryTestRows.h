/**
 * @file FStaticDataRepositoryTestRows.h
 * @brief 静态数据仓库测试行结构体（模块内自动化测试专用，非业务数据）
 */

#pragma once

#include "CoreMinimal.h"

#include "FStaticDataRepositoryTestRows.generated.h"

/**
 * @brief 测试行结构 A（纯标量字段，用于二进制序列化往返精度比对）
 */
USTRUCT(BlueprintType)
struct FRepositoryTestRowA {
    GENERATED_BODY()

public:
    /**
     * @brief 整型分数属性
     */
    UPROPERTY()
    int32 Score = 0;

    /**
     * @brief 浮点权重属性
     */
    UPROPERTY()
    float Weight = 0.0f;
};

/**
 * @brief 测试行结构 B（含字符串与动态数组容器字段，覆盖复合序列化路径）
 */
USTRUCT(BlueprintType)
struct FRepositoryTestRowB {
    GENERATED_BODY()

public:
    /**
     * @brief 文本标签属性
     */
    UPROPERTY()
    FString Label;

    /**
     * @brief 整型数组数值列表
     */
    UPROPERTY()
    TArray<int32> Values;

    /**
     * @brief 布尔标记位
     */
    UPROPERTY()
    bool bFlag = false;
};
