/**
 * @file GameFeedbackFlashTracker.h
 * @brief 受击闪白 Overlay 生命周期追踪（绝对时间续期、弱存活令牌防 UAF 与安全还原）
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Engine/TimerHandle.h"
#include "Feedback/GameFeedbackActions.h"
#include "UObject/ObjectKey.h"

class UMaterialInterface;
class UMeshComponent;
class UWorld;

/**
 * @brief 维护网格组件 Overlay 材质与到期定时器，保证续期与还原安全
 *
 * 到期定时器随 UWorld::GetTimerManager() 与游戏暂停态保持一致，同时以弱存活令牌与析构兜底彻底杜绝 UAF。
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
     * @brief 默认构造闪白追踪器并初始化存活令牌
     */
    FGameFeedbackFlashTracker() = default;

    /**
     * @brief 析构函数：自动兜底还原全部追踪网格并失效回调令牌，杜绝定时器 UAF
     */
    ~FGameFeedbackFlashTracker();

    FGameFeedbackFlashTracker(const FGameFeedbackFlashTracker&) = delete;
    FGameFeedbackFlashTracker& operator=(const FGameFeedbackFlashTracker&) = delete;
    FGameFeedbackFlashTracker(FGameFeedbackFlashTracker&&) = delete;
    FGameFeedbackFlashTracker& operator=(FGameFeedbackFlashTracker&&) = delete;

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
     * @brief 还原全部网格 Overlay 并清空追踪（子系统注销或析构时调用）
     * @param World 当前世界
     */
    void Shutdown(UWorld* World);

    /**
     * @brief 清理失效网格记录并同步取消残留定时器（释放已销毁网格所持有的强引用材质）
     * @param World 可选当前世界（非空时同步清除失效记录绑定的定时器）
     */
    void PruneInvalid(UWorld* World = nullptr);

    /**
     * @brief 获取当前追踪的记录数量（供测试与可观测性）
     * @return 追踪记录数量
     */
    [[nodiscard]] int32 GetTrackedCount() const noexcept {
        return Records.Num();
    }

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
    [[nodiscard]] double GetCurrentTime() const;

    /**
     * @brief 挂载带弱存活令牌保护的到期定时器
     * @param World 目标世界
     * @param TimerHandle 输出定时器句柄
     * @param MeshKey 目标网格不可变对象键
     * @param DelaySeconds 延迟秒数
     */
    void ArmExpirationTimer(UWorld& World, FTimerHandle& TimerHandle, TObjectKey<UMeshComponent> MeshKey, float DelaySeconds);

    /**
     * @brief 到期回调按真实时间还原 Overlay；提前触发时按剩余时间重新 arm
     * @param MeshKey 目标网格不可变对象键
     */
    void OnExpiration(TObjectKey<UMeshComponent> MeshKey);

    TSharedRef<bool> AliveToken = MakeShared<bool>(true);
    TWeakObjectPtr<UWorld> BoundWorld;
    TFunction<double()> TimeProvider;
    TMap<TObjectKey<UMeshComponent>, FRecord> Records;
};
