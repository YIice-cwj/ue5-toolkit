/**
 * @file FGameDamageContext.h
 * @brief 泛型伤害分发上下文契约（Tag 驱动稀疏通道 · 纯泛型）
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"

#include "Damage/FGameDamageChannel.h"

#include "FGameDamageContext.generated.h"

/**
 * @brief 泛型伤害分发的输入上下文
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageContext {
    GENERATED_BODY()

public:
    FGameDamageContext() = default;

    /**
     * @brief 获取指定通道伤害数值（支持层级标签匹配，如 Damage.Physical 匹配 Damage.Physical.Slash）
     * @param InChannelTag 待匹配的通道标签
     * @return 匹配通道伤害数值之和
     */
    [[nodiscard]] float GetChannelDamage(const FGameplayTag& InChannelTag) const noexcept {
        return FGameDamageChannel::GetSum(Channels, InChannelTag);
    }

    /**
     * @brief 获取指定通道伤害数值（精确标签匹配）
     * @param InChannelTag 待匹配的通道标签
     * @return 伤害数值；未命中返回 0.0f
     */
    [[nodiscard]] float GetChannelDamageExact(const FGameplayTag& InChannelTag) const noexcept {
        return FGameDamageChannel::GetExact(Channels, InChannelTag);
    }

    /**
     * @brief 设置或覆盖指定通道的伤害数值
     * @param InChannelTag 目标通道标签
     * @param InMagnitude 赋予的伤害数值
     */
    void SetChannelDamage(const FGameplayTag& InChannelTag, float InMagnitude) {
        FGameDamageChannel::SetExact(Channels, InChannelTag, InMagnitude);
    }

    /**
     * @brief 累加指定通道的伤害数值
     * @param InChannelTag 目标通道标签
     * @param InDelta 增量数值
     */
    void AddChannelDamage(const FGameplayTag& InChannelTag, float InDelta) {
        FGameDamageChannel::AddExact(Channels, InChannelTag, InDelta);
    }

    /**
     * @brief 获取所有通道基础伤害的总和
     * @return 基础伤害总和
     */
    [[nodiscard]] float GetTotalBaseDamage() const noexcept {
        float Sum = 0.0f;
        for (const FGameDamageChannel& Item : Channels) {
            Sum += Item.Magnitude;
        }
        return Sum;
    }

    /**
     * @brief 伤害通道列表容器（支持多通道伤害同时结算）
     */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TArray<FGameDamageChannel> Channels;

    /**
     * @brief 引擎底层物理命中上下文（包含命中点、法线、受击骨骼等）
     */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FHitResult HitResult;

    /**
     * @brief 伤害发起者（逻辑主体，通常为发射技能的角色或 Controller）
     */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TWeakObjectPtr<AActor> Instigator = nullptr;

    /**
     * @brief 伤害造成者（物理实体，通常为发射出的投掷物、陷阱或武器）
     */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TWeakObjectPtr<AActor> DamageCauser = nullptr;

    /**
     * @brief 伤害特质标签（例如 Damage.Trait.Crit、Damage.Trait.Backstab）
     */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTagContainer TraitTags;

    /**
     * @brief 附加上下文环境标签（例如 Environment.Underwater）
     */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTagContainer ContextTags;
};
