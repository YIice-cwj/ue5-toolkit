/**
 * @file GameFeedbackVFXExecutor.h
 * @brief 粒子特效无状态执行器（纯决策计划、软引用去重异步预热与朝向降级）
 */

#pragma once

#include "CoreMinimal.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackSignal.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UPrimitiveComponent;
class UWorld;

namespace GameFeedback::Execution {

/**
 * @brief 粒子特效执行决策模式
 */
enum class EGameFeedbackVFXSpawnMode : uint8 {
    Skipped,
    AsyncPreload,
    SpawnAtLocation,
    SpawnAttached
};

/**
 * @brief 粒子特效纯执行计划（分离决策计算与引擎渲染副作用，支持无头环境精确验证）
 */
struct FGameFeedbackVFXSpawnPlan {
    EGameFeedbackVFXSpawnMode Mode = EGameFeedbackVFXSpawnMode::Skipped;
    UNiagaraSystem* System = nullptr;
    UPrimitiveComponent* AttachComponent = nullptr;
    FName BoneName = NAME_None;
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
};

/**
 * @brief 按显式信号方向计算粒子发射朝向（自动归一化反射法线并在缺失轨迹时降级至表面法线）
 * @param Signal 表现信号
 * @param OrientationMode 粒子朝向模式
 * @return 粒子世界旋转
 */
[[nodiscard]] FRotator ResolveVFXRotation(const FGameFeedbackSignal& Signal, EGameFeedbackVFXOrientation OrientationMode);

/**
 * @brief 纯逻辑推导本次粒子特效的执行计划（阈值门禁、DS 短路、异步预热、附着回退与位姿计算）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 粒子参数
 * @return 确定的粒子执行计划
 */
[[nodiscard]] FGameFeedbackVFXSpawnPlan ResolveVFXSpawnPlan(
    UWorld* World,
    const FGameFeedbackSignal& Signal,
    const FGameFeedbackVFXAction& Action);

/**
 * @brief 在世界中发射受击粒子特效（软引用未就绪时经 DefinitionProvider 去重异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 粒子参数
 * @return 成功生成的 Niagara 组件指针；被门禁拦截、DS 短路、无头渲染禁用或异步预热时返回 nullptr
 */
UNiagaraComponent* SpawnVFX(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackVFXAction& Action);

}  // namespace GameFeedback::Execution
