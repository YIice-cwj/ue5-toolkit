/**
 * @file UGameAbilitySystemComponent.h
 * @brief 通用能力系统组件基类声明（默认开启网络复制 · 支持对象池安全复用）
 */

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Core/IGamePoolable.h"

#include "UGameAbilitySystemComponent.generated.h"

/**
 * @brief 通用能力系统组件基类
 *
 * 职责：在组件构造期开启复制（bReplicates 默认 false，SetIsReplicatedByDefault 为 protected，
 * 须由子类构造调用），保证 SpawnedAttributes 与 GAS 属性可复制到客户端；
 * 实现 IGamePoolable 契约，支持对象池归还时静默清理运行态能力、活跃效果、标签计数与宿主信息。
 */
UCLASS()
class GAMEGAMEPLAY_API UGameAbilitySystemComponent : public UAbilitySystemComponent, public IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：开启默认复制，并把 GE 与能力容器注册到自身拥有者
     */
    UGameAbilitySystemComponent();

    /**
     * @brief 初始化能力宿主信息（确保 AbilityActorInfo 已分配，交由基类建立宿主关联并记录日志）
     * @param InOwnerActor 拥有者 Actor
     * @param InAvatarActor 附身 Actor
     */
    void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;

    /**
     * @brief 从对象池激活回调：确保容器拥有者指针保持绑定当前组件实例
     */
    void OnAcquireFromPool_Implementation() override;

    /**
     * @brief 归还对象池回调：取消并清理权威能力、活跃效果、持续 GameplayCue 与复制标签映射，静默重置阻断/松标签计数与宿主信息
     */
    void OnReleaseToPool_Implementation() override;
};
