// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Time/TimeDilationTypes.h"

#include "TimeDilationSubsystem.generated.h"

/**
 * @brief 分层时间膨胀与 HitStop 顿帧管理器
 *
 * 管理 Actor 局部定格与全局分层时间缩放，支持通道优先级覆盖与平滑恢复。
 */
UCLASS()
class GAMECORE_API UTimeDilationSubsystem : public UTickableWorldSubsystem {
    GENERATED_BODY()

public:
    UTimeDilationSubsystem();

    /**
     * @brief 获取世界子系统引用（断言有效）
     * @param WorldContextObject 上下文对象
     * @return 子系统引用
     */
    static UTimeDilationSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取世界子系统指针（静默返回空）
     * @param WorldContextObject 上下文对象
     * @return 子系统指针
     */
    static UTimeDilationSubsystem* GetPtr(const UObject* WorldContextObject);

    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;

    /**
     * @brief 每帧推进时间层叠（暂停期间仍刷新 LastRealTime，避免解除暂停时时间突跳）
     */
    void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    bool IsTickableWhenPaused() const override;

    /**
     * @brief 请求对单个 Actor 施加打击顿帧
     * @param Target 目标 Actor
     * @param Duration 顿帧持续真实时间（秒）
     * @param TimeDilation 顿帧期间局部时间缩放
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void RequestHitStop(AActor* Target, float Duration, float TimeDilation = 0.001f);

    /**
     * @brief 清除指定 Actor 的打击顿帧并立即还原时间缩放
     * @param Target 目标 Actor
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearHitStop(AActor* Target);

    /**
     * @brief 检查指定 Actor 当前是否正处于打击顿帧中
     * @param Target 目标 Actor
     * @return 处于顿帧中返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Time")
    bool IsActorInHitStop(const AActor* Target) const;

    /**
     * @brief 清除所有 Actor 的打击顿帧
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearAllHitStops();

    /**
     * @brief 在指定通道请求全局时间膨胀
     * @param Channel 目标通道
     * @param Duration 持续时间（秒，<=0 表示持久有效直至手动清除）
     * @param TimeDilation 目标时间缩放
     * @param BlendOutDuration 结束时的平滑淡出时间（秒）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void RequestGlobalDilation(ETimeDilationChannel Channel, float Duration, float TimeDilation, float BlendOutDuration = 0.0f);

    /**
     * @brief 清除指定通道的全局时间膨胀
     * @param Channel 目标通道
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearGlobalDilation(ETimeDilationChannel Channel);

    /**
     * @brief 清除指定通道的全局时间膨胀，并支持平滑淡出恢复
     * @param Channel 目标通道
     * @param BlendOutDuration 淡出过渡时间（秒）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearGlobalDilationWithBlend(ETimeDilationChannel Channel, float BlendOutDuration = 0.2f);

    /**
     * @brief 清除所有通道的全局时间膨胀
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearAllGlobalDilations();

    /**
     * @brief 获取当前生效的最终全局时间缩放值
     * @return 最终全局时间缩放
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Time")
    float GetEffectiveGlobalTimeDilation() const;

    /**
     * @brief 获取指定通道当前的活跃状态
     * @param Channel 目标通道
     * @return 通道是否处于激活状态
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Time")
    bool IsChannelActive(ETimeDilationChannel Channel) const;

    /**
     * @brief 获取指定通道当前的时间缩放值
     * @param Channel 目标通道
     * @return 通道的时间缩放值（未激活返回 1.0f）
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Time")
    float GetChannelTimeDilation(ETimeDilationChannel Channel) const;

    /**
     * @brief 手动推进内部真实时间步长（用于单测或自定义时钟驱动）
     * @param RealDeltaSeconds 真实流逝秒数
     */
    void AdvanceTime(float RealDeltaSeconds);

private:
    /**
     * @brief 依据通道优先级重算生效的全局时间缩放
     *
     * 实现思路：通道按 ETimeDilationChannel 枚举顺序即优先级（数值越小优先级越高），
     * 取首个激活通道的当前缩放作为生效值并立即退出，高优先级完全覆盖低优先级（不做乘积/最小值合成）
     */
    void UpdateEffectiveGlobalTimeDilation();

    /**
     * @brief 将生效时间缩放写回世界设置
     * @param NewTimeDilation 生效时间缩放
     */
    void ApplyGlobalTimeDilationToWorld(float NewTimeDilation);

private:
    UPROPERTY(Transient)
    TArray<FGlobalChannelState> ChannelStates;

    double LastRealTime = 0.0;
    float EffectiveGlobalTimeDilation = 1.0f;
    TMap<TWeakObjectPtr<AActor>, FActorHitStopState> ActiveActorHitStops;
};

