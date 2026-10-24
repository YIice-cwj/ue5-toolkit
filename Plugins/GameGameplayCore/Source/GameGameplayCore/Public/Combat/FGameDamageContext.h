/**
 * @file FGameDamageContext.h
 * @brief 泛型伤害分发契约（Tag 驱动稀疏通道，100% 游戏无关）
 *
 * 设计思路：框架层定义泛型分发上下文，不包含任何具体游戏业务字段（如物理/术法）；
 * 伤害轨道全面由 FGameDamageChannel（Tag-Magnitude）承载，支持任意游戏题材（奇幻/科幻/现代军事）；
 * 各种受击目标（GAS 角色 / Mass 适配器 / 可破坏物 / 载具）统一通过 IDamageable 消费本结构。
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"

#include "GameGameplayCore.h"
#include "FGameDamageContext.generated.h"

/**
 * @brief 单个伤害通道（Tag-Magnitude 对）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageChannel {
    GENERATED_BODY()

public:
    FGameDamageChannel() = default;
    FGameDamageChannel(const FGameplayTag& InTag, float InMag) : ChannelTag(InTag), Magnitude(InMag) {}

    /** 伤害通道标签（如 Damage.Channel.Physical, Damage.Channel.Spell, Damage.Channel.Fire） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTag ChannelTag;

    /** 伤害基础数值 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Damage")
    float Magnitude = 0.0f;

    [[nodiscard]] static const FGameDamageChannel* FindExact(const TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag) noexcept {
        return InChannels.FindByPredicate([&InTag](const FGameDamageChannel& Item) {
            return Item.ChannelTag.MatchesTagExact(InTag);
        });
    }

    [[nodiscard]] static FGameDamageChannel* FindExactMutable(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag) noexcept {
        return InChannels.FindByPredicate([&InTag](const FGameDamageChannel& Item) {
            return Item.ChannelTag.MatchesTagExact(InTag);
        });
    }

    [[nodiscard]] static float GetExact(const TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag, float DefaultValue = 0.0f) noexcept {
        const FGameDamageChannel* Found = FindExact(InChannels, InTag);
        return Found != nullptr ? Found->Magnitude : DefaultValue;
    }

    [[nodiscard]] static float GetSum(const TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag) noexcept {
        float Sum = 0.0f;
        for (const FGameDamageChannel& Item : InChannels) {
            if (Item.ChannelTag.MatchesTag(InTag)) {
                Sum += Item.Magnitude;
            }
        }
        return Sum;
    }

    static void SetExact(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag, float InMagnitude) {
        if (FGameDamageChannel* Found = FindExactMutable(InChannels, InTag)) {
            Found->Magnitude = InMagnitude;
        } else {
            InChannels.Emplace(InTag, InMagnitude);
        }
    }

    static void AddExact(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag, float InDelta) {
        if (FGameDamageChannel* Found = FindExactMutable(InChannels, InTag)) {
            Found->Magnitude += InDelta;
        } else {
            InChannels.Emplace(InTag, InDelta);
        }
    }
};

/**
 * @brief 泛型伤害分发的输入上下文
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageContext {
    GENERATED_BODY()

public:
    FGameDamageContext() = default;

    /**
     * @brief 获取指定通道伤害数值（支持 GameplayTag 层级继承，如父标签匹配子标签）
     * @param InChannelTag 待匹配的通道标签
     * @return 匹配通道伤害数值之和，未匹配返回 0.0f
     */
    [[nodiscard]] float GetChannelDamage(const FGameplayTag& InChannelTag) const noexcept {
        return FGameDamageChannel::GetSum(Channels, InChannelTag);
    }

    /**
     * @brief 获取指定通道伤害数值（严格精确标签匹配）
     * @param InChannelTag 待匹配的通道标签
     * @return 伤害数值，未匹配返回 0.0f
     */
    [[nodiscard]] float GetChannelDamageExact(const FGameplayTag& InChannelTag) const noexcept {
        return FGameDamageChannel::GetExact(Channels, InChannelTag);
    }

    /**
     * @brief 设置或覆盖指定通道伤害数值（精确匹配现有通道）
     * @param InChannelTag 目标通道标签
     * @param InMagnitude 伤害数值
     */
    void SetChannelDamage(const FGameplayTag& InChannelTag, float InMagnitude) {
        FGameDamageChannel::SetExact(Channels, InChannelTag, InMagnitude);
    }

    /**
     * @brief 累加指定通道伤害数值（精确匹配现有通道）
     * @param InChannelTag 目标通道标签
     * @param InDelta 增量数值
     */
    void AddChannelDamage(const FGameplayTag& InChannelTag, float InDelta) {
        FGameDamageChannel::AddExact(Channels, InChannelTag, InDelta);
    }

    /**
     * @brief 获取所有通道基础伤害总和
     * @return 基础伤害总和
     */
    [[nodiscard]] float GetTotalBaseDamage() const noexcept {
        float Sum = 0.0f;
        for (const FGameDamageChannel& Item : Channels) {
            Sum += Item.Magnitude;
        }
        return Sum;
    }

    /** 伤害通道容器（Tag 驱动稀疏通道） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TArray<FGameDamageChannel> Channels;

    /** 物理命中上下文 */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FHitResult HitResult;

    /** 伤害发起者（逻辑主体，如 Controller） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TWeakObjectPtr<AActor> Instigator = nullptr;

    /** 伤害造成者（物理实体，如子弹、法术投射物、武器） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TWeakObjectPtr<AActor> DamageCauser = nullptr;

    /** 伤害特质标签（约定标签集，如 Damage.Trait.Fire, Damage.Trait.Holy，框架零题材假定） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTagContainer TraitTags;

    /** 附带上下文标签（如 Weapon.Type.Sword, Attack.Heavy） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTagContainer ContextTags;
};
