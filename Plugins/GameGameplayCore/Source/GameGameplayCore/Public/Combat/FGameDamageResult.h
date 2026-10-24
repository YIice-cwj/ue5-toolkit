/**
 * @file FGameDamageResult.h
 * @brief 打击反馈契约（纯粹泛型 · 全 Tag 驱动 · 零具体业务假设）
 *
 * 设计思路：
 * 彻底消除命中、致死、暴击、硬直、卡肉等任何具体玩法硬编码，所有状态与动态数值完全由开发者通过 GameplayTag 驱动。
 * 仅保留实际生效数值（TotalActualDamage）、各通道数值明细（ActualChannels）、
 * 状态标签容器（FeedbackTags）与动态参数容器（DynamicValues）。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "Combat/FGameDamageContext.h"

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
     * @brief 获取指定通道实际伤害数值（支持 GameplayTag 层级继承）
     * @param InChannelTag 待匹配的通道标签
     * @return 实际伤害数值之和，未匹配返回 0.0f
     */
    [[nodiscard]] float GetActualChannelDamage(const FGameplayTag& InChannelTag) const noexcept {
        return FGameDamageChannel::GetSum(ActualChannels, InChannelTag);
    }

    /**
     * @brief 获取指定通道实际伤害数值（严格精确标签匹配）
     * @param InChannelTag 待匹配的通道标签
     * @return 实际伤害数值，未匹配返回 0.0f
     */
    [[nodiscard]] float GetActualChannelDamageExact(const FGameplayTag& InChannelTag) const noexcept {
        return FGameDamageChannel::GetExact(ActualChannels, InChannelTag);
    }

    /**
     * @brief 设置实际生效的总伤害数值
     * @param InActualDamage 实际生效伤害数值
     */
    FORCEINLINE void SetActualDamage(float InActualDamage) noexcept {
        TotalActualDamage = InActualDamage;
    }

    /**
     * @brief 查询是否包含特定反馈标签（如命中、致死、暴击、硬直、格挡、特定受击材质等）
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
     * @brief 获取动态数值参数（如卡肉时长、护盾吸收量、躯干削减值等）
     * @param InTag 数值属性标签
     * @param DefaultValue 未找到时的默认值
     * @return 属性数值
     */
    [[nodiscard]] float GetDynamicValue(const FGameplayTag& InTag, float DefaultValue = 0.0f) const noexcept {
        return FGameDamageChannel::GetExact(DynamicValues, InTag, DefaultValue);
    }

    /**
     * @brief 设置或覆盖动态数值参数
     * @param InTag 数值属性标签
     * @param InValue 数值
     */
    void SetDynamicValue(const FGameplayTag& InTag, float InValue) {
        FGameDamageChannel::SetExact(DynamicValues, InTag, InValue);
    }

    /**
     * @brief 检查是否存在指定动态数值参数
     * @param InTag 数值属性标签
     * @return 存在返回 true
     */
    [[nodiscard]] bool HasDynamicValue(const FGameplayTag& InTag) const noexcept {
        return FGameDamageChannel::FindExact(DynamicValues, InTag) != nullptr;
    }

    /** 实际生效的总数值 */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    float TotalActualDamage = 0.0f;

    /** 各通道实际生效伤害容器 */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    TArray<FGameDamageChannel> ActualChannels;

    /** 反馈状态标签容器（由开发者自由注册与判断，如命中、致死、暴击、格挡、硬直、受击材质等） */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    FGameplayTagContainer FeedbackTags;

    /** 动态数值参数容器（由开发者自由注册，如卡肉停顿时长、护盾吸收量、架势削弱量等） */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    TArray<FGameDamageChannel> DynamicValues;
};
