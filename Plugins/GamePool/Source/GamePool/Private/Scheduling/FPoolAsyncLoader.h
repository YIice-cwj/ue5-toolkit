/**
 * @file FPoolAsyncLoader.h
 * @brief 事件驱动异步资产流式加载与 C++20 协程分步预热内部调度器
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Async/TGameCoroTask.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"

class AActor;
class UObject;
class FActorPoolManager;
class FUObjectPoolManager;

/**
 * @brief 异步资产流式加载与协程分步预热内部调度器（纯静态工具类，带弱实现体存活守卫防反初始化悬垂）
 */
class FPoolAsyncLoader {
public:
    FPoolAsyncLoader() = delete;

    /**
     * @brief 异步分步预热指定 Actor 类别（受 MaxIdleCount 容量上限与帧预算时间切片约束）
     * @param WorldContextObject 世界上下文或子系统宿主对象
     * @param ImplGuard 所属子系统 Pimpl 存活弱引用守卫
     * @param ActorManager Actor 池管理器引用
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 目标预热实例数量
     * @param MaxSpawnsPerStep 单步最大实例化配额（防单帧顿卡）
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功生成的空闲实例数量异步协程任务
     */
    static TGameCoroTask<int32> PrewarmActorsAsync(
        UObject* WorldContextObject,
        TWeakPtr<const void> ImplGuard,
        FActorPoolManager& ActorManager,
        TSubclassOf<AActor> ActorClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 事件驱动异步流式加载并分步预热 Actor 软引用资产（零忙等轮询）
     * @param WorldContextObject 世界上下文或子系统宿主对象
     * @param ImplGuard 所属子系统 Pimpl 存活弱引用守卫
     * @param ActorManager Actor 池管理器引用
     * @param ActorClassPath 目标 Actor 类软引用路径
     * @param PrewarmCount 目标预热实例数量
     * @param MaxSpawnsPerStep 单步最大实例化配额
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功生成的空闲实例数量异步协程任务
     */
    static TGameCoroTask<int32> PrewarmActorsByPathAsync(
        UObject* WorldContextObject,
        TWeakPtr<const void> ImplGuard,
        FActorPoolManager& ActorManager,
        const FSoftObjectPath& ActorClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 异步分步预热指定纯 UObject 类别（受 MaxIdleCount 容量上限与帧预算时间切片约束）
     * @param WorldContextObject 世界上下文或子系统宿主对象
     * @param ImplGuard 所属子系统 Pimpl 存活弱引用守卫
     * @param ObjectManager UObject 池管理器引用
     * @param ObjectClass 目标 UObject 类
     * @param PrewarmCount 目标预热实例数量
     * @param MaxSpawnsPerStep 单步最大实例化配额
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功生成的空闲实例数量异步协程任务
     */
    static TGameCoroTask<int32> PrewarmObjectsAsync(
        UObject* WorldContextObject,
        TWeakPtr<const void> ImplGuard,
        FUObjectPoolManager& ObjectManager,
        TSubclassOf<UObject> ObjectClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 事件驱动异步流式加载并分步预热纯 UObject 软引用资产（零忙等轮询）
     * @param WorldContextObject 世界上下文或子系统宿主对象
     * @param ImplGuard 所属子系统 Pimpl 存活弱引用守卫
     * @param ObjectManager UObject 池管理器引用
     * @param ObjectClassPath 目标 UObject 类软引用路径
     * @param PrewarmCount 目标预热实例数量
     * @param MaxSpawnsPerStep 单步最大实例化配额
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功生成的空闲实例数量异步协程任务
     */
    static TGameCoroTask<int32> PrewarmObjectsByPathAsync(
        UObject* WorldContextObject,
        TWeakPtr<const void> ImplGuard,
        FUObjectPoolManager& ObjectManager,
        const FSoftObjectPath& ObjectClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );
};
