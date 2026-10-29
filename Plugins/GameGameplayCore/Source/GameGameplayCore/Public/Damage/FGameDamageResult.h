/**
 * @file FGameDamageResult.h
 * @brief 打击反馈契约（纯粹泛型 · 全 Tag 驱动 · 零具体业务假设）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "Damage/FGameDamageChannel.h"

#include "FGameDamageResult.generated.h"

/**
 * @brief 单次伤害结算的打击反馈
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageResult {
    GENERATED_BODY()

public:
    FGameDamageResult() = default;

    /**
     * @brief 获取指定通道实际生效的伤害数值（支持层级标签继承）
     * @param InChannelTag 待匹配的通道标签
     * @return 实际伤害数值之和
     */
    [[nodiscard]] float GetActualChannelDamage(const FGameplayTag& InChannelTag) const noexcept {
        return FGameDamageChannel::GetSum(ActualChannels, InChannelTag);
    }

    /**
     * @brief 获取指定通道实际生效的伤害数值（精确标签匹配）
     * @param InChannelTag 待匹配的通道标签
     * @return 实际伤害数值；未找到返回 0.0f
     */
    [[nodiscard]] float GetActualChannelDamageExact(const FGameplayTag& InChannelTag) const noexcept {
        return FGameDamageChannel::GetExact(ActualChannels, InChannelTag);
    }

    /**
     * @brief 设置实际生效的总伤害数值
     * @param InActualDamage 实际生效的总伤害数值
     */
    FORCEINLINE void SetActualDamage(float InActualDamage) noexcept {
        TotalActualDamage = InActualDamage;
    }

    /**
     * @brief 查询是否包含特定反馈标签（如 Feedback.Hit.Crit、Feedback.Hit.Blocked）
     * @param InTag 待检测的反馈标签
     * @return 存在或其父标签匹配返回 true
     */
    [[nodiscard]] FORCEINLINE bool HasFeedbackTag(const FGameplayTag& InTag) const noexcept {
        return FeedbackTags.HasTag(InTag);
    }

    /**
     * @brief 添加反馈标签
     * @param InTag 待添加的反馈标签
     */
    FORCEINLINE void AddFeedbackTag(const FGameplayTag& InTag) noexcept {
        FeedbackTags.AddTag(InTag);
    }

    /**
     * @brief 移除反馈标签
     * @param InTag 待移除的反馈标签
     */
    FORCEINLINE void RemoveFeedbackTag(const FGameplayTag& InTag) noexcept {
        FeedbackTags.RemoveTag(InTag);
    }

    /**
     * @brief 获取动态数值参数（例如护盾抵消量、额外反伤量）
     * @param InTag 数值属性标签
     * @param DefaultValue 未找到时的默认回退值
     * @return 属性数值；未找到返回 DefaultValue
     */
    [[nodiscard]] float GetDynamicValue(const FGameplayTag& InTag, float DefaultValue = 0.0f) const noexcept {
        return FGameDamageChannel::GetExact(DynamicValues, InTag, DefaultValue);
    }

    /**
     * @brief 设置或覆盖动态数值参数
     * @param InTag 数值属性标签
     * @param InValue 赋予的数值
     */
    void SetDynamicValue(const FGameplayTag& InTag, float InValue) {
        FGameDamageChannel::SetExact(DynamicValues, InTag, InValue);
    }

    /**
     * @brief 检查是否存在指定动态数值参数
     * @param InTag 数值属性标签
     * @return 若已记录该数值条目返回 true，否则返回 false
     */
    [[nodiscard]] bool HasDynamicValue(const FGameplayTag& InTag) const noexcept {
        return FGameDamageChannel::FindExact(DynamicValues, InTag) != nullptr;
    }

    /**
     * @brief 最终实际扣减/结算生效的总数值
     */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    float TotalActualDamage = 0.0f;

    /**
     * @brief 各伤害通道实际生效的分解伤害容器
     */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    TArray<FGameDamageChannel> ActualChannels;

    /**
     * @brief 打击反馈状态标签集合（用于表现层播放受击特效、暴击跳字等）
     */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    FGameplayTagContainer FeedbackTags;

    /**
     * @brief 结算产生的扩展动态数值参数容器
     */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    TArray<FGameDamageChannel> DynamicValues;
};
