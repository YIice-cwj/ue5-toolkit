/**
 * @file FGameDebugTimeController.h
 * @brief 现代化运行时时间膨胀、慢动作、暂停与逐帧单步调试控制器（支持 GM 交互与编译期零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"
#include "Config/GameCompileSwitches.h"

class UWorld;

/**
 * @brief 全局时间与慢动作调试控制器
 */
class GAMEDEBUG_API FGameDebugTimeController {
public:
    /**
     * @brief 初始化并注册时间控制相关的 GM 指令
     */
    static void Initialize();

    /**
     * @brief 设置指定世界的全局时间膨胀缩放比例
     * @param WorldContextObject 世界上下文对象
     * @param TimeDilation 时间缩放倍率（0.001f ~ 20.0f）
     * @return 设置成功返回 true
     */
    static bool SetTimeDilation(const UObject* WorldContextObject, float TimeDilation);

    /**
     * @brief 获取指定世界的当前全局时间膨胀缩放比例
     * @param WorldContextObject 世界上下文对象
     * @return 当前时间缩放倍率（世界无效时返回 1.0f）
     */
    [[nodiscard]] static float GetTimeDilation(const UObject* WorldContextObject);

    /**
     * @brief 暂停世界时钟（自动记忆暂停前的时间倍率）
     * @param WorldContextObject 世界上下文对象
     * @return 执行成功返回 true
     */
    static bool Pause(const UObject* WorldContextObject);

    /**
     * @brief 恢复世界时钟（优先恢复至暂停前记忆的倍率，如未暂停过则恢复为 1.0f）
     * @param WorldContextObject 世界上下文对象
     * @return 执行成功返回 true
     */
    static bool Resume(const UObject* WorldContextObject);

    /**
     * @brief 判定当前世界时钟是否处于暂停/冻结状态
     * @param WorldContextObject 世界上下文对象
     * @return 已暂停或时间膨胀率接近 0 返回 true
     */
    [[nodiscard]] static bool IsPaused(const UObject* WorldContextObject);

    /**
     * @brief 强制重置世界时钟为 1.0f
     * @param WorldContextObject 世界上下文对象
     * @return 执行成功返回 true
     */
    static bool ResetTime(const UObject* WorldContextObject);

    /**
     * @brief 单步逐帧步进世界时钟（恢复运行指定帧数后自动重新暂停）
     * @param WorldContextObject 世界上下文对象
     * @param Frames 步进帧数（默认 1 帧）
     * @return 执行成功返回 true
     */
    static bool StepFrame(const UObject* WorldContextObject, int32 Frames = 1);

    /**
     * @brief 获取用于操作的有效 UWorld 指针
     * @param WorldContextObject 世界上下文对象
     * @return UWorld 指针
     */
    [[nodiscard]] static UWorld* ResolveWorld(const UObject* WorldContextObject);

private:
    static inline float PrePauseTimeDilation = 1.0f;
};
