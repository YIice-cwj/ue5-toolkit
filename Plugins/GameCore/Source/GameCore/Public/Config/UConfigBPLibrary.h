/**
 * @file UConfigBPLibrary.h
 * @brief 配置注册表 Blueprint 反射变体桥接（模板注册表对 BP 不可见，经本库暴露）
 *
 * 设计思路：桥接能力与 Game::Config::FConfigVariant 的可用类型保持对称——标量/字符串之外补齐引擎数学
 * 类型（FVector / FRotator）与标签类型（FGameplayTag）；未注册键一律返回调用方默认值，
 * 写入统一经 FConfigRegistry::SetConfig（只读键拒绝语义由注册表保证）。
 * 通过 WorldContextObject 穿透支持 World 级实例隔离，杜绝蓝图绕过 World 隔离。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "UConfigBPLibrary.generated.h"

/**
 * @brief 配置注册表蓝图桥接库（按值取值，未注册返回默认值，支持 World 上下文隔离）
 */
UCLASS()
class GAMECORE_API UConfigBPLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 按值获取布尔型配置
     * @param Key 配置键名称
     * @param bDefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的布尔值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static bool GetConfigBool(FName Key, bool bDefaultValue = false, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取 32 位整型配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 int32 值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static int32 GetConfigInt(FName Key, int32 DefaultValue = 0, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取 64 位整型配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 int64 值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static int64 GetConfigInt64(FName Key, int64 DefaultValue = 0, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取单精度浮点型配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 float 值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static float GetConfigFloat(FName Key, float DefaultValue = 0.0f, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取双精度浮点型配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 double 值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static double GetConfigDouble(FName Key, double DefaultValue = 0.0, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取字符串配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 FString 值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static FString GetConfigString(FName Key, const FString& DefaultValue = TEXT(""), const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取名称类型配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 FName 值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static FName GetConfigName(FName Key, FName DefaultValue = NAME_None, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取三维向量配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 FVector 值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static FVector GetConfigVector(FName Key, FVector DefaultValue = FVector::ZeroVector, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取旋转体配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 FRotator 值或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static FRotator GetConfigRotator(FName Key, FRotator DefaultValue = FRotator::ZeroRotator, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 按值获取 GameplayTag 标签配置
     * @param Key 配置键名称
     * @param DefaultValue 未注册或类型不匹配时的回退默认值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 配置的 FGameplayTag 标签或回退默认值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static FGameplayTag GetConfigGameplayTag(FName Key, FGameplayTag DefaultValue, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 查询配置键是否已注册
     * @param Key 配置键名称
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     * @return 若已注册返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static bool IsConfigRegistered(FName Key, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入布尔型配置
     * @param Key 配置键名称
     * @param bValue 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigBool(FName Key, bool bValue, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入 32 位整型配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigInt(FName Key, int32 Value, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入 64 位整型配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigInt64(FName Key, int64 Value, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入单精度浮点型配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigFloat(FName Key, float Value, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入双精度浮点型配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigDouble(FName Key, double Value, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入字符串配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigString(FName Key, const FString& Value, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入名称类型配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigName(FName Key, FName Value, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入三维向量配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigVector(FName Key, FVector Value, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入旋转体配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigRotator(FName Key, FRotator Value, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 运行时覆盖写入 GameplayTag 标签配置
     * @param Key 配置键名称
     * @param Value 新配置值
     * @param WorldContextObject 世界上下文对象（蓝图隐式绑定）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Config", meta = (WorldContext = "WorldContextObject"))
    static void SetConfigGameplayTag(FName Key, FGameplayTag Value, const UObject* WorldContextObject = nullptr);
};
