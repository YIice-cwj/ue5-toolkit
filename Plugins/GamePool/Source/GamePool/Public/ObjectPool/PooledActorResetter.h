// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObjectPool/ObjectPoolTypes.h"

/**
 * @brief 池化 Actor 深度组件状态重置与清洗工具
 */
class FPooledActorResetter {
public:
    /**
     * @brief 根据指定的重置位掩码深度重置 Actor 的所有子组件与引擎状态
     * @param Actor 待重置的 Actor 实例
     * @param ResetFlags 重置策略位掩码
     */
    static void ResetActorComponents(AActor* Actor, EPoolResetFlags ResetFlags);

    /**
     * @brief 安全调用受管对象的池化接口生命周期回调
     * @param TargetObject 目标对象实例
     * @param bAcquire 若为 true 触发借出唤醒回调，若为 false 触发归还休眠回调
     */
    static void InvokePoolableCallback(UObject* TargetObject, bool bAcquire);
};
