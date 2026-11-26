/**
 * @file FGameDebugTimeController.h
 * @brief 现代化运行时时间膨胀、慢动作、按世界隔离暂停记忆与逐帧单步调试控制器（支持 GM 交互与编译期零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * @brief 全局时间与慢动作调试控制器（纯静态不可实例化，内部按 UWorld 隔离多 PIE 暂停记忆与步进状态）
 */
class GAMEDEBUG_API FGameDebugTimeController final {
public:
    static constexpr float MinTimeDilation = 0.001f;
    static constexpr float MaxTimeDilation = 20.0f;
    static constexpr float PausedTimeDilation = 0.0001f;
    static constexpr float PauseDetectionThreshold = 0.0005f;

public:
    FGameDebugTimeController() = delete;
    ~FGameDebugTimeController() = delete;

    FGameDebugTimeController(const FGameDebugTimeController&) = delete;
    FGameDebugTimeController& operator=(const FGameDebugTimeController&) = delete;
    FGameDebugTimeController(FGameDebugTimeController&&) = delete;
    FGameDebugTimeController& operator=(FGameDebugTimeController&&) = delete;

    /**
     * @brief 初始化并注册时间控制相关的 GM 指令（幂等，模块启动时调用）
     */
    static void Initialize();

    /**
     * @brief 注销全部待执行单步步进 Ticker、清空世界状态表并卸载时间控制 GM 指令（模块卸载时对称调用）
     */
    static void Shutdown();

    /**
     * @brief 设置指定世界的全局时间膨胀缩放比例（自动拦截 NaN/Inf，并钳制至 [MinTimeDilation, MaxTimeDilation]）
     * @param WorldContextObject 世界上下文对象（传 nullptr 时按活跃 GameWorld 兜底；传非空无效对象返回 false）
     * @param TimeDilation 时间缩放倍率（合法范围 [0.001f, 20.0f]）
     * @return 设置成功返回 true
     */
    static bool SetTimeDilation(const UObject* WorldContextObject, float TimeDilation);

    /**
     * @brief 获取指定世界的当前全局时间膨胀缩放比例
     * @param WorldContextObject 世界上下文对象
     * @return 当前时间缩放倍率（世界无效或开关关闭时返回 1.0f）
     */
    [[nodiscard]] static float GetTimeDilation(const UObject* WorldContextObject);

    /**
     * @brief 暂停世界时钟（写入 PausedTimeDilation 并按世界独立记忆暂停前的正常倍率）
     * @param WorldContextObject 世界上下文对象
     * @return 执行成功且实际倍率降至暂停阈值以下返回 true
     */
    static bool Pause(const UObject* WorldContextObject);

    /**
     * @brief 恢复世界时钟（优先恢复至该世界暂停前记忆的倍率，若未记忆过则恢复为 1.0f）
     * @param WorldContextObject 世界上下文对象
     * @return 执行成功返回 true
     */
    static bool Resume(const UObject* WorldContextObject);

    /**
     * @brief 判定当前世界时钟是否处于暂停/冻结状态（<= PauseDetectionThreshold）
     * @param WorldContextObject 世界上下文对象
     * @return 已暂停返回 true
     */
    [[nodiscard]] static bool IsPaused(const UObject* WorldContextObject);

    /**
     * @brief 强制重置指定世界时钟及暂停记忆倍率为 1.0f
     * @param WorldContextObject 世界上下文对象
     * @return 执行成功返回 true
     */
    static bool ResetTime(const UObject* WorldContextObject);

    /**
     * @brief 单步逐帧步进世界时钟（按记忆速度运行指定完整帧数后自动重新暂停至 PausedTimeDilation，自动补偿 UWorld::Tick 中途调用的同帧 CoreTicker）
     * @param WorldContextObject 世界上下文对象
     * @param Frames 步进帧数（下限钳制为 1 帧）
     * @return 执行成功返回 true
     */
    static bool StepFrame(const UObject* WorldContextObject, int32 Frames = 1);
};
