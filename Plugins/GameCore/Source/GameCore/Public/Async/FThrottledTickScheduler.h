/**
 * @file FThrottledTickScheduler.h
 * @brief 纯 C++ 分级交错节流心跳调度器（机制与引擎外壳彻底分离）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FThrottledTickTypes.h"

/**
 * @brief 分级心跳节流调度器核心（零 UWorld 依赖，可在任何多线程、独立离线工具或纯 C++ 单测中独立运行）
 */
class GAMECORE_API FThrottledTickScheduler {
public:
    using FThrottledTickCallback = TFunction<void(float DeltaTime)>;
    using FOwnerValidator = TFunction<bool()>;

private:
    struct FThrottledEntry {
        uint64 HandleId = 0ULL;
        FOwnerValidator OwnerValidator;
        FThrottledTickCallback Callback;
        float Interval = 0.0f;
        float Elapsed = 0.0f;
        bool bHasOwner = false;
        bool bPendingRemoval = false;
    };

public:
    FThrottledTickScheduler() = default;
    ~FThrottledTickScheduler() = default;

    FThrottledTickScheduler(const FThrottledTickScheduler&) = delete;
    FThrottledTickScheduler& operator=(const FThrottledTickScheduler&) = delete;
    FThrottledTickScheduler(FThrottledTickScheduler&&) noexcept = default;
    FThrottledTickScheduler& operator=(FThrottledTickScheduler&&) noexcept = default;

    /**
     * @brief 推进调度器中心跳时间
     * @param DeltaTime 帧流逝时间
     */
    void Tick(float DeltaTime);

    /**
     * @brief 注册无宿主绑定的全局节流心跳
     * @param IntervalSeconds 间隔秒数（<= 0 视为每帧全速更新）
     * @param Callback 心跳回调闭包
     * @return 分配的句柄 ID
     */
    uint64 RegisterIntervalTick(
        float IntervalSeconds,
        FThrottledTickCallback Callback
    );

    /**
     * @brief 注册自定义秒级间隔节流心跳（通用校验器闭包）
     * @param InValidator 宿主生命周期有效性校验器
     * @param IntervalSeconds 间隔秒数（<= 0 视为每帧全速更新）
     * @param Callback 心跳回调闭包
     * @return 分配的句柄 ID（0 代表失败）
     */
    uint64 RegisterIntervalTick(
        FOwnerValidator InValidator,
        float IntervalSeconds,
        FThrottledTickCallback Callback
    );

    /**
     * @brief 针对 UE5 UObject 弱引用的重载注册接口
     * @param Owner 宿主对象弱引用
     * @param IntervalSeconds 间隔秒数
     * @param Callback 心跳回调闭包
     * @return 分配的句柄 ID
     */
    uint64 RegisterIntervalTick(
        TWeakObjectPtr<UObject> Owner,
        float IntervalSeconds,
        FThrottledTickCallback Callback
    );

    /**
     * @brief 依句柄 ID 注销心跳任务
     * @param HandleId 目标句柄 ID
     * @return 存在有效任务并成功标记注销返回 true
     */
    bool UnregisterTick(uint64 HandleId);

    /**
     * @brief 动态调整指定任务的间隔秒数
     * @param HandleId 目标句柄 ID
     * @param NewIntervalSeconds 新间隔秒数
     * @return 调整成功返回 true
     */
    bool SetTickInterval(uint64 HandleId, float NewIntervalSeconds);

    /**
     * @brief 查询目标任务是否处于活跃注册态
     * @param HandleId 目标句柄 ID
     * @return 活跃态返回 true
     */
    [[nodiscard]] bool IsTickActive(uint64 HandleId) const;

    /**
     * @brief 查询当前已注册的心跳任务总数
     * @return 任务总数
     */
    [[nodiscard]] int32 GetRegisteredCount() const;

    /**
     * @brief 清空全部已注册心跳任务
     */
    void ClearAll();

    /**
     * @brief 查询是否无活跃任务
     * @return 为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return Entries.IsEmpty();
    }

    /**
     * @brief 将枚举频率转换为秒数间隔
     * @param Rate 频率档位
     * @return 对应秒数
     */
    [[nodiscard]] static float RateToIntervalSeconds(EThrottledRate Rate) noexcept;

private:
    TArray<FThrottledEntry> Entries;
    uint64 NextHandleId = 1ULL;
    bool bIsIterating = false;
};
