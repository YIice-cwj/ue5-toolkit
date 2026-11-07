/**
 * @file UThrottledTickerSubsystem.h
 * @brief 分级交错节流心跳调度子系统（削峰平摊长周期心跳，零注册时休眠零 Tick 开销）
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Tasks/FThrottledTickTypes.h"

#include "UThrottledTickerSubsystem.generated.h"

/**
 * @brief 世界级分级心跳节流调度子系统
 */
UCLASS()
class GAMECORE_API UThrottledTickerSubsystem : public UTickableWorldSubsystem {
    GENERATED_BODY()

public:
    using FThrottledTickCallback = TFunction<void(float DeltaTime)>;

private:
    struct FThrottledEntry {
        uint64 HandleId = 0ULL;
        TWeakObjectPtr<UObject> Owner;
        FThrottledTickCallback Callback;
        float Interval = 0.0f;
        float Elapsed = 0.0f;
        bool bHasOwner = false;
        bool bPendingRemoval = false;
    };

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
     * @brief 依句柄 ID 注销心跳任务
     * @param HandleId 目标任务句柄 ID
     * @return 存在有效任务并标记注销返回 true
     */
    bool UnregisterTick(uint64 HandleId);

    /**
     * @brief 动态调整指定任务的心跳频率档位
     * @param HandleId 目标任务句柄 ID
     * @param NewRate 新预设频率
     * @return 调整成功返回 true
     */
    bool SetTickRate(uint64 HandleId, EThrottledRate NewRate);

    /**
     * @brief 动态调整指定任务的自定义间隔秒数
     * @param HandleId 目标任务句柄 ID
     * @param NewIntervalSeconds 新间隔秒数
     * @return 调整成功返回 true
     */
    bool SetTickInterval(uint64 HandleId, float NewIntervalSeconds);

    /**
     * @brief 查询目标任务是否处于活跃注册态
     * @param HandleId 目标任务句柄 ID
     * @return 活跃态返回 true
     */
    [[nodiscard]] bool IsTickActive(uint64 HandleId) const;

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
     * @brief 将枚举频率转换为对应的秒数间隔
     * @param Rate 频率档位
     * @return 对应秒数（每帧返回 0.0f）
     */
    [[nodiscard]] static float RateToIntervalSeconds(EThrottledRate Rate) noexcept;

private:
    TArray<FThrottledEntry> Entries;
    uint64 NextHandleId = 1ULL;
    bool bIsIterating = false;
};
