/**
 * @file FThrottledTickTypes.h
 * @brief 分级节流心跳调度类型定义与操作句柄
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UThrottledTickerSubsystem;

/**
 * @brief 预设分级节流心跳频率枚举
 */
UENUM(BlueprintType)
enum class EThrottledRate : uint8 {
    /**
     * @brief 每帧更新（全速无节流）
     */
    EveryFrame = 0,
    /**
     * @brief 约 30Hz 更新（间隔约 0.033s）
     */
    Rate30Hz = 1,
    /**
     * @brief 约 10Hz 更新（间隔约 0.1s）
     */
    Rate10Hz = 2,
    /**
     * @brief 约 2Hz 更新（间隔约 0.5s）
     */
    Rate2Hz = 3,
    /**
     * @brief 约 1Hz 更新（间隔约 1.0s）
     */
    Rate1Hz = 4
};

/**
 * @brief 节流心跳任务轻量操作句柄（可安全拷贝，支持主动解挂与动态调频）
 */
class GAMECORE_API FThrottledTickHandle {
public:
    FThrottledTickHandle() = default;

    /**
     * @brief 解挂并注销关联的节流心跳任务（幂等安全）
     * @return 成功找到并注销活跃任务返回 true
     */
    bool Unregister();

    /**
     * @brief 动态调整节流心跳频率档位
     * @param NewRate 新预设频率
     * @return 调整成功返回 true
     */
    bool SetRate(EThrottledRate NewRate);

    /**
     * @brief 动态调整自定义秒级节流间隔
     * @param NewIntervalSeconds 新间隔秒数（小于等于 0 视为每帧执行）
     * @return 调整成功返回 true
     */
    bool SetInterval(float NewIntervalSeconds);

    /**
     * @brief 查询当前心跳任务是否处于活跃注册态
     * @return 活跃态返回 true
     */
    [[nodiscard]] bool IsActive() const;

    /**
     * @brief 获取内部唯一句柄 ID
     * @return 句柄 ID（0 表示空句柄）
     */
    [[nodiscard]] uint64 GetHandleId() const noexcept {
        return HandleId;
    }

private:
    friend class UThrottledTickerSubsystem;

    FThrottledTickHandle(UThrottledTickerSubsystem* InSubsystem, uint64 InHandleId) noexcept;

private:
    TWeakObjectPtr<UThrottledTickerSubsystem> SubsystemWeak;
    uint64 HandleId = 0ULL;
};
