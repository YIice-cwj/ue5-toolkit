/**
 * @file GameFeedbackFlashTracker.h
 * @brief 受击闪白 Overlay 生命周期追踪（绝对时间续期与安全还原）
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Feedback/GameFeedbackActions.h"
#include "TimerManager.h"

class UMaterialInterface;
class UMeshComponent;
class UWorld;

/**
 * @brief 维护网格组件 Overlay 材质与到期定时器，保证续期与还原安全
 */
class FGameFeedbackFlashTracker {
private:
    struct FRecord {
        TStrongObjectPtr<UMaterialInterface> OriginalOverlayMaterial = nullptr;
        double ExpirationRealTime = 0.0;
        FTimerHandle TimerHandle;
    };

public:
    /**
     * @brief 默认构造闪白追踪器
     */
    FGameFeedbackFlashTracker() = default;

    /**
     * @brief 对目标应用受击闪白（同一网格多次命中按绝对到期时间平滑续期）
     * @param World 当前世界
     * @param Target 受击目标
     * @param HitResult 命中结果（精确模式据此取出被击中的网格组件）
     * @param Action 闪白参数
     * @param Magnitude 本次表现强度
     */
    void Execute(UWorld* World, AActor* Target, const FHitResult& HitResult, const FGameFeedbackHitFlashAction& Action, float Magnitude);

    /**
     * @brief 还原指定 Actor 上全部网格的原始 Overlay 并移除追踪
     * @param World 当前世界
     * @param Target 待重置目标
     */
    void ClearActor(UWorld* World, AActor* Target);

    /**
     * @brief 还原全部网格 Overlay 并清空追踪（子系统注销时调用）
     * @param World 当前世界
     */
    void Shutdown(UWorld* World);

    /**
     * @brief 设置时间提供者（用于测试或可控时钟注入）
     * @param InTimeProvider 返回当前时间（秒）的函数对象
     */
    void SetTimeProvider(TFunction<double()> InTimeProvider);

private:
    /**
     * @brief 获取当前时间（秒），优先使用注入的时间提供者
     * @return 当前时间（秒）
     */
    double GetCurrentTime() const;

    /**
     * @brief 到期回调按真实时间还原 Overlay；提前触发时按剩余时间重新 arm
     * @param WeakMesh 目标网格弱引用
     */
    void OnExpiration(TWeakObjectPtr<UMeshComponent> WeakMesh);

    TFunction<double()> TimeProvider;
    TMap<TWeakObjectPtr<UMeshComponent>, FRecord> Records;
};
