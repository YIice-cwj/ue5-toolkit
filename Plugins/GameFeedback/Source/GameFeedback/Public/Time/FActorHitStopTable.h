/**
 * @file FActorHitStopTable.h
 * @brief Actor 局部顿帧表（纯 C++ · 合并规则、到期还原与失效清理）
 */

#pragma once

#include "CoreMinimal.h"
#include "Time/TimeDilationTypes.h"

class AActor;

/**
 * @brief Actor 局部顿帧表
 */
struct GAMEFEEDBACK_API FActorHitStopTable {
    /**
     * @brief 请求对单个 Actor 施加顿帧（同一 Actor 重复请求取较大时长与较小缩放）
     * @param InTarget 目标 Actor
     * @param InDuration 顿帧持续真实时间（秒）
     * @param InTimeDilation 顿帧期间局部时间缩放（钳制到 [0.0001, 20]）
     */
    void Request(AActor& InTarget, float InDuration, float InTimeDilation);

    /**
     * @brief 清除指定 Actor 的顿帧并立即还原时间缩放
     * @param InTarget 目标 Actor
     */
    void Clear(AActor* InTarget);

    /**
     * @brief 清除全部顿帧并立即还原各自时间缩放
     */
    void ClearAll();

    /**
     * @brief 检查指定 Actor 是否处于顿帧中
     * @param InTarget 目标 Actor
     * @return 处于顿帧中返回 true
     */
    [[nodiscard]] bool IsActive(const AActor* InTarget) const;

    /**
     * @brief 推进顿帧计时，到期或目标失效时还原时间缩放并移除记录
     * @param InRealDeltaSeconds 真实流逝秒数
     */
    void Advance(float InRealDeltaSeconds);

    /**
     * @brief 清理目标已失效的记录（不归还时间缩放）
     */
    void PruneInvalid();

private:
    TMap<TWeakObjectPtr<AActor>, FActorHitStopState> ActiveActorHitStops;
};
