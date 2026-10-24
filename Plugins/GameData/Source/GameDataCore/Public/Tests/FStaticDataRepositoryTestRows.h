/**
 * @file FStaticDataRepositoryTestRows.h
 * @brief 静态数据仓库测试行结构体（模块内自动化测试专用，非业务数据）
 */

#pragma once

#include "CoreMinimal.h"

#include "FStaticDataRepositoryTestRows.generated.h"

/** 测试行结构 A（标量字段，往返比对用） */
USTRUCT(BlueprintType)
struct FRepositoryTestRowA {
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 Score = 0;

    UPROPERTY()
    float Weight = 0.0f;
};

/** 测试行结构 B（容器字段，覆盖数组序列化路径） */
USTRUCT(BlueprintType)
struct FRepositoryTestRowB {
    GENERATED_BODY()

public:
    UPROPERTY()
    FString Label;

    UPROPERTY()
    TArray<int32> Values;

    UPROPERTY()
    bool bFlag = false;
};
