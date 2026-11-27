/**
 * @file FActorHitStopTable.h
 * @brief Actor 局部顿帧表（纯 C++ · 合并规则、到期还原与失效清理）
 */

#pragma once

#include "CoreMinimal.h"
#include "Time/TimeDilationTypes.h"
#include "UObject/ObjectKey.h"

class AActor;

/**
 * @brief Actor 局部顿帧表
 */
struct GAMEFEEDBACK_API FActorHitStopTable {
    /**
     * @brief 请求对单个 Actor 施加顿帧（同一 Actor 重复请求取较大时长与较小缩放；InDuration <= 0 时忽略）
     * @param InTarget 目标 Actor
     * @param InDuration 顿帧持续真实时间（秒，须 > 0）
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
     * @brief 检查当前是否存在任何活跃的 Actor 顿帧记录
     * @return 存在活跃记录返回 true
     */
    [[nodiscard]] bool HasActive() const noexcept {
        return !ActiveActorHitStops.IsEmpty();
    }

    /**
     * @brief 推进顿帧计时，到期或目标失效时还原时间缩放并移除记录（InRealDeltaSeconds <= 0 时忽略）
     * @param InRealDeltaSeconds 真实流逝秒数
     */
    void Advance(float InRealDeltaSeconds);

    /**
     * @brief 清理目标已失效或正在销毁的记录（不归还时间缩放）
     */
    void PruneInvalid();

private:
    TMap<TObjectKey<AActor>, FActorHitStopState> ActiveActorHitStops;
};
