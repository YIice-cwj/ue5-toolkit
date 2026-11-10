/**
 * @file UThrottledTickerSubsystem.h
 * @brief 分级交错节流心跳调度子系统（削峰平摊长周期心跳，零注册时休眠零 Tick 开销）
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Async/FThrottledTickTypes.h"
#include "Async/FThrottledTickScheduler.h"

#include "UThrottledTickerSubsystem.generated.h"

/**
 * @brief 世界级分级心跳节流调度子系统（引擎外壳 Thin-Shell，核心算法委托给 FThrottledTickScheduler）
 */
UCLASS()
class GAMECORE_API UThrottledTickerSubsystem : public UTickableWorldSubsystem {
    GENERATED_BODY()

public:
    using FThrottledTickCallback = FThrottledTickScheduler::FThrottledTickCallback;

public:
    /**
     * @brief 世界注销生命周期回调：清空全部注册的心跳任务
     */
    void Deinitialize() override;

    /**
     * @brief 每帧推进各个节流桶的心跳任务，自动剔除失效宿主与待注销项
     * @param DeltaTime 帧流逝时间
     */
    void Tick(float DeltaTime) override;

    /**
     * @brief 统计性能组标识
     * @return Tickable 统计 ID
     */
    TStatId GetStatId() const override;

    /**
     * @brief 仅当存在活跃注册项且非类默认对象时参与引擎 Tick
     * @return 需要 Tick 返回 true
     */
    bool IsTickable() const override;

public:
    /**
     * @brief 注册预设频率分级节流心跳（自动分相交错平摊调用峰值）
     * @param Owner 宿主对象弱引用（宿主销毁时自动解挂注销）
     * @param Rate 预设节流频率档位
     * @param Callback 心跳回调闭包（入参为自上次触发以来的实际累积流逝时间）
     * @return 可操作句柄
     */
    FThrottledTickHandle RegisterTick(
        TWeakObjectPtr<UObject> Owner,
        EThrottledRate Rate,
        FThrottledTickCallback Callback
    );

    /**
     * @brief 注册自定义秒级间隔节流心跳
     * @param Owner 宿主对象弱引用
     * @param IntervalSeconds 间隔秒数（小于等于 0 视为每帧全速更新）
     * @param Callback 心跳回调闭包
     * @return 可操作句柄
     */
    FThrottledTickHandle RegisterIntervalTick(
        TWeakObjectPtr<UObject> Owner,
        float IntervalSeconds,
        FThrottledTickCallback Callback
    );

    /**
     * @brief 查询当前已注册的心跳任务总数
     * @return 任务总数
     */
    [[nodiscard]] int32 GetRegisteredCount() const;

    /**
     * @brief 清空全部已注册的心跳任务
     */
    void ClearAll();

    /**
     * @brief 获取世界上下文绑定的节流心跳子系统单例
     * @param WorldContextObject 世界上下文对象
     * @return 子系统实例引用
     */
    [[nodiscard]] static UThrottledTickerSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取世界上下文绑定的节流心跳子系统指针（空指针保护）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统实例指针，获取失败返回 nullptr
     */
    [[nodiscard]] static UThrottledTickerSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 获取底层节流心跳调度内核可变引用
     * @return 节流心跳调度内核引用
     */
    [[nodiscard]] FThrottledTickScheduler& GetScheduler() noexcept {
        return Scheduler;
    }

    /**
     * @brief 获取底层节流心跳调度内核常量引用
     * @return 节流心跳调度内核常量引用
     */
    [[nodiscard]] const FThrottledTickScheduler& GetScheduler() const noexcept {
        return Scheduler;
    }

private:
    FThrottledTickScheduler Scheduler;
};
