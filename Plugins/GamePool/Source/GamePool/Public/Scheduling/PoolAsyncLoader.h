// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Async/TGameCoroTask.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"

class AActor;
class UObject;
class FActorPoolManager;
class FUObjectPoolManager;

/**
 * @brief 异步资产流式加载与协程分步预热调度器
 */
class GAMEPOOL_API FPoolAsyncLoader {
public:
    /**
     * @brief 异步解析软引用路径，若未载入则通过 StreamableManager 流式加载
     * @param WorldContextObject 世界上下文对象
     * @param ClassPath 目标类软引用路径
     * @param CancellationToken 协程取消令牌
     * @return 成功加载的 UClass 指针，取消或加载失败返回 nullptr
     */
    static TGameCoroTask<UClass*> ResolveOrAsyncLoadClass(
        UObject* WorldContextObject,
        const FSoftObjectPath& ClassPath,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 异步分步预热指定 Actor 类别
     * @param WorldContextObject 世界上下文对象
     * @param ActorManager Actor 池管理器
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 目标预热实例数量
     * @param MaxSpawnsPerStep 单步最大实例化配额（防单帧顿卡）
     * @param CancellationToken 协程取消令牌
     * @return 实际成功生成的空闲实例数量
     */
    static TGameCoroTask<int32> PrewarmActorsAsync(
        UObject* WorldContextObject,
        FActorPoolManager& ActorManager,
        TSubclassOf<AActor> ActorClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 异步流式加载并分步预热 Actor 软引用资产
     * @param WorldContextObject 世界上下文对象
     * @param ActorManager Actor 池管理器
     * @param ActorClassPath 目标 Actor 类软引用路径
     * @param PrewarmCount 目标预热实例数量
     * @param MaxSpawnsPerStep 单步最大实例化配额
     * @param CancellationToken 协程取消令牌
     * @return 实际成功生成的空闲实例数量，若加载失败或取消返回 0
     */
    static TGameCoroTask<int32> PrewarmActorsByPathAsync(
        UObject* WorldContextObject,
        FActorPoolManager& ActorManager,
        const FSoftObjectPath& ActorClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 异步分步预热指定纯 UObject 类别
     * @param WorldContextObject 世界上下文对象
     * @param ObjectManager UObject 池管理器
     * @param ObjectClass 目标 UObject 类
     * @param PrewarmCount 目标预热实例数量
     * @param MaxSpawnsPerStep 单步最大实例化配额
     * @param CancellationToken 协程取消令牌
     * @return 实际成功生成的空闲实例数量
     */
    static TGameCoroTask<int32> PrewarmObjectsAsync(
        UObject* WorldContextObject,
        FUObjectPoolManager& ObjectManager,
        TSubclassOf<UObject> ObjectClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 异步流式加载并分步预热纯 UObject 软引用资产
     * @param WorldContextObject 世界上下文对象
     * @param ObjectManager UObject 池管理器
     * @param ObjectClassPath 目标 UObject 类软引用路径
     * @param PrewarmCount 目标预热实例数量
     * @param MaxSpawnsPerStep 单步最大实例化配额
     * @param CancellationToken 协程取消令牌
     * @return 实际成功生成的空闲实例数量，若加载失败或取消返回 0
     */
    static TGameCoroTask<int32> PrewarmObjectsByPathAsync(
        UObject* WorldContextObject,
        FUObjectPoolManager& ObjectManager,
        const FSoftObjectPath& ObjectClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );
};
