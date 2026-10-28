/**
 * @file UConfigBPLibrary.h
 * @brief 配置注册表 Blueprint 反射变体桥接（模板注册表对 BP 不可见，经本库暴露）
 *
 * 设计思路：桥接能力与 Game::Config::FConfigVariant 的可用类型保持对称——标量/字符串之外补齐引擎数学
 * 类型（FVector / FRotator）与标签类型（FGameplayTag）；未注册键一律返回调用方默认值，
 * 写入统一经 FConfigRegistry::SetConfig（只读键拒绝语义由注册表保证）。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "UConfigBPLibrary.generated.h"

/**
 * @brief 配置注册表蓝图桥接库（按值取值，未注册返回默认值）
 */
UCLASS()
class GAMECORE_API UConfigBPLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /** 按值获取 bool 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static bool GetConfigBool(FName Key, bool bDefaultValue = false);

    /** 按值获取 int32 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static int32 GetConfigInt(FName Key, int32 DefaultValue = 0);

    /** 按值获取 int64 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static int64 GetConfigInt64(FName Key, int64 DefaultValue = 0);

    /** 按值获取 float 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static float GetConfigFloat(FName Key, float DefaultValue = 0.0f);

    /** 按值获取 double 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static double GetConfigDouble(FName Key, double DefaultValue = 0.0);

    /** 按值获取 FString 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static FString GetConfigString(FName Key, const FString& DefaultValue = TEXT(""));

    /** 按值获取 FName 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static FName GetConfigName(FName Key, FName DefaultValue = NAME_None);

    /** 按值获取 FVector 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static FVector GetConfigVector(FName Key, FVector DefaultValue = FVector::ZeroVector);

    /** 按值获取 FRotator 配置（未注册返回默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static FRotator GetConfigRotator(FName Key, FRotator DefaultValue = FRotator::ZeroRotator);

    /** 按值获取 FGameplayTag 配置（未注册返回传入的默认值；UHT 不支持标签类型默认参数，故无默认值） */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static FGameplayTag GetConfigGameplayTag(FName Key, FGameplayTag DefaultValue);

    /**
     * @brief 查询配置键是否已注册
     * @param Key 配置键
     * @return 已注册返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config")
    static bool IsConfigRegistered(FName Key);

    /** 运行时覆盖 bool 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigBool(FName Key, bool bValue);

    /** 运行时覆盖 int32 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigInt(FName Key, int32 Value);

    /** 运行时覆盖 int64 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigInt64(FName Key, int64 Value);

    /** 运行时覆盖 float 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigFloat(FName Key, float Value);

    /** 运行时覆盖 double 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigDouble(FName Key, double Value);

    /** 运行时覆盖 FString 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigString(FName Key, const FString& Value);

    /** 运行时覆盖 FName 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigName(FName Key, FName Value);

    /** 运行时覆盖 FVector 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigVector(FName Key, FVector Value);

    /** 运行时覆盖 FRotator 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigRotator(FName Key, FRotator Value);

    /** 运行时覆盖 FGameplayTag 配置 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config")
    static void SetConfigGameplayTag(FName Key, FGameplayTag Value);
};
