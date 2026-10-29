/**
 * @file FGameDamageChannel.h
 * @brief 单个伤害通道（Tag-Magnitude 对）及静态辅助运算
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "FGameDamageChannel.generated.h"

/**
 * @brief 单个伤害通道（Tag-Magnitude 对）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageChannel {
    GENERATED_BODY()

public:
    FGameDamageChannel() = default;

    /**
     * @brief 构造函数：以通道标签与基础数值创建条目
     * @param InTag 伤害通道 GameplayTag（例如 Damage.Type.Physical）
     * @param InMag 通道数值大小
     */
    FGameDamageChannel(const FGameplayTag& InTag, float InMag) : ChannelTag(InTag), Magnitude(InMag) {}

    /**
     * @brief 伤害通道标签
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTag ChannelTag;

    /**
     * @brief 伤害通道数值大小
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Damage")
    float Magnitude = 0.0f;

    /**
     * @brief 在通道列表中精确查找指定标签的通道条目（常量版本）
     * @param InChannels 源通道数组
     * @param InTag 目标精确标签
     * @return 匹配的条目只读指针；未命中返回 nullptr
     */
    [[nodiscard]] static const FGameDamageChannel* FindExact(const TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag) noexcept {
        return InChannels.FindByPredicate([&InTag](const FGameDamageChannel& Item) {
            return Item.ChannelTag.MatchesTagExact(InTag);
        });
    }

    /**
     * @brief 在通道列表中精确查找指定标签的通道条目（可变版本）
     * @param InChannels 源通道数组
     * @param InTag 目标精确标签
     * @return 匹配的条目可变指针；未命中返回 nullptr
     */
    [[nodiscard]] static FGameDamageChannel* FindExactMutable(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag) noexcept {
        return InChannels.FindByPredicate([&InTag](const FGameDamageChannel& Item) {
            return Item.ChannelTag.MatchesTagExact(InTag);
        });
    }

    /**
     * @brief 精确读取指定通道的数值
     * @param InChannels 源通道数组
     * @param InTag 目标精确标签
     * @param DefaultValue 未找到通道时的回退默认值
     * @return 通道数值；若未命中则返回 DefaultValue
     */
    [[nodiscard]] static float GetExact(const TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag, float DefaultValue = 0.0f) noexcept {
        const FGameDamageChannel* Found = FindExact(InChannels, InTag);
        return Found != nullptr ? Found->Magnitude : DefaultValue;
    }

    /**
     * @brief 汇总指定标签及其所有子标签的通道数值总和
     * @param InChannels 源通道数组
     * @param InTag 父标签或精确标签
     * @return 匹配到的所有通道数值之和
     */
    [[nodiscard]] static float GetSum(const TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag) noexcept {
        float Sum = 0.0f;
        for (const FGameDamageChannel& Item : InChannels) {
            if (Item.ChannelTag.MatchesTag(InTag)) {
                Sum += Item.Magnitude;
            }
        }
        return Sum;
    }

    /**
     * @brief 精确设置指定通道的数值（不存在则自动追加新条目）
     * @param InChannels 目标通道数组
     * @param InTag 目标通道标签
     * @param InMagnitude 赋予的通道数值
     */
    static void SetExact(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag, float InMagnitude) {
        if (FGameDamageChannel* Found = FindExactMutable(InChannels, InTag)) {
            Found->Magnitude = InMagnitude;
        } else {
            InChannels.Emplace(InTag, InMagnitude);
        }
    }

    /**
     * @brief 精确累加指定通道的数值（不存在则自动追加初始数值为 InDelta 的条目）
     * @param InChannels 目标通道数组
     * @param InTag 目标通道标签
     * @param InDelta 累加的增量数值
     */
    static void AddExact(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag, float InDelta) {
        if (FGameDamageChannel* Found = FindExactMutable(InChannels, InTag)) {
            Found->Magnitude += InDelta;
        } else {
            InChannels.Emplace(InTag, InDelta);
        }
    }
};
