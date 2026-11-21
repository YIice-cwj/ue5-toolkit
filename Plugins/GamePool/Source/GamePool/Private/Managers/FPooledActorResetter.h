/**
 * @file FPooledActorResetter.h
 * @brief 池化 Actor 单次遍历深度组件重置与 IGamePoolable 生命周期回调派发工具
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/FPoolAcquirePayload.h"
#include "Core/ObjectPoolTypes.h"
#include "GameFramework/Actor.h"

/**
 * @brief 池化对象生命周期回调安全派发与 Actor 深度状态重置内部工具类
 */
class FPooledActorResetter {
public:
    FPooledActorResetter() = delete;

    /**
     * @brief 按重置位掩码单次遍历并深度清洗 Actor 的所有子组件与引擎状态
     * @param Actor 待重置的 Actor 实例
     * @param ResetFlags 重置策略位掩码
     */
    static void ResetActorComponents(AActor* Actor, EPoolResetFlags ResetFlags);

    /**
     * @brief 安全派发 IGamePoolable 的借出/归还生命周期回调（纯 C++ 走直调快路径，蓝图子类走 BlueprintNativeEvent 执行链）
     * @param TargetObject 目标对象实例
     * @param bAcquire 若为 true 触发借出唤醒回调，若为 false 触发归还休眠回调
     */
    static void InvokePoolableCallback(UObject* TargetObject, bool bAcquire);

    /**
     * @brief 安全派发 IGamePoolable 的带载荷借出回调
     * @param TargetObject 目标对象实例
     * @param Payload 带编译期类型指纹的业务载荷视图
     */
    static void InvokePoolablePayloadCallback(UObject* TargetObject, FPoolAcquirePayload Payload);
};
