/**
 * @file UGameAbilitySystemComponent.h
 * @brief 通用能力系统组件基类声明
 */

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"

#include "UGameAbilitySystemComponent.generated.h"

/**
 * @brief 通用能力系统组件基类
 *
 * 职责：在组件构造期开启复制（bReplicates 默认 false，SetIsReplicatedByDefault 为 protected，
 * 须由子类构造调用），保证 SpawnedAttributes 与 GAS 属性可复制到客户端；
 * 后续 GA/GE/预测等扩展点统一落于此子类。
 */
UCLASS()
class GAMEGAMEPLAY_API UGameAbilitySystemComponent : public UAbilitySystemComponent {
    GENERATED_BODY()

public:
    UGameAbilitySystemComponent();

    void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
};
