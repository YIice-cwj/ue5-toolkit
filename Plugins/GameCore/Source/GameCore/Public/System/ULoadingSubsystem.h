/**
 * @file ULoadingSubsystem.h
 * @brief 通用关卡流转与加载幕布子系统
 *
 * 设计思路：在子系统 Initialize / Deinitialize 生命周期内自动挂钩与注销
 * FCoreUObjectDelegates::PreLoadMap / PostLoadMapWithWorld 委托，对外提供标准关卡跳转与加载态广播。
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ULoadingSubsystem.generated.h"

/**
 * @brief 关卡加载状态发生变动时的多播委托声明
 * @param bIsLoading 是否正在加载关卡
 * @param TargetLevelName 目标关卡名称
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadingStateChanged, bool, bIsLoading, FName, TargetLevelName);

/**
 * @brief 全局加载与关卡流转子系统
 *
 * 继承自 UGameInstanceSubsystem，作为跨关卡加载控制中枢。
 * 负责平滑关卡跳转调度、全屏遮罩/幕布广播以及引擎底层地图加载生命周期事件监听。
 */
UCLASS(BlueprintType)
class GAMECORE_API ULoadingSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 静态工具方法：通过任意上下文对象获取当前加载子系统
     * @param WorldContextObject 世界上下文对象
     * @return 加载子系统指针，不存在返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Loading", meta = (WorldContext = "WorldContextObject"))
    static ULoadingSubsystem* Get(const UObject* WorldContextObject);

    /**
     * @brief 加载状态变动多播委托（关卡加载开始与结束时广播）
     */
    UPROPERTY(BlueprintAssignable, Category = "GameCore|Loading")
    FOnLoadingStateChanged OnLoadingStateChanged;

    /**
     * @brief 子系统初始化生命周期回调：挂钩引擎 PreLoadMap 与 PostLoadMapWithWorld 委托
     * @param Collection 子系统管理器集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统销毁生命周期回调：注销引擎地图加载委托
     */
    void Deinitialize() override;

    /**
     * @brief 携带加载幕布事件的标准关卡跳转接口
     * @param LevelName 目标关卡名称
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Loading")
    void OpenLevelWithLoading(const FName& LevelName);

    /**
     * @brief 查询当前是否正处于关卡流转或加载状态中
     * @return 若正在加载返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Loading")
    bool IsLoading() const;

    /**
     * @brief 获取当前正在加载的目标关卡名称
     * @return 目标关卡名称；未加载时返回 NAME_None
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Loading")
    FName GetTargetLevelName() const;

private:
    /**
     * @brief 引擎开始加载地图委托回调
     * @param MapName 即将加载的目标地图路径
     */
    void OnPreLoadMap(const FString& MapName);

    /**
     * @brief 引擎完成加载地图委托回调
     * @param LoadedWorld 加载完成的新世界对象指针
     */
    void OnPostLoadMap(UWorld* LoadedWorld);

private:
    /**
     * @brief 当前是否正处于关卡加载中
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Loading", meta = (AllowPrivateAccess = "true"))
    bool bIsLoading = false;

    /**
     * @brief 当前加载目标关卡名称
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Loading", meta = (AllowPrivateAccess = "true"))
    FName TargetLevelName = NAME_None;
};
