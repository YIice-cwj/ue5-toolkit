/**
 * @file FGameGameplayEffectContext.h
 * @brief 打击反馈 GameplayEffect 上下文（全 Tag 驱动 · 泛型打击反馈契约）
 *
 * 设计思路：彻底消除对致死、卡肉等特定机制的硬编码字段假设；
 * 对齐 FGameDamageResult 架构，内置 TotalActualDamage、ActualChannels、FeedbackTags 与 DynamicValues；
 * 一切状态（命中/致死/暴击/格挡/硬直）均由 GameplayTag 驱动，一切数值（卡肉/削韧/护盾抵扣）均由动态通道承载；
 * 与引擎 FGameplayEffectContext 深度整合，支持 NetSerialize 网络全同步与 Duplicate 深度复制。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Combat/FGameDamageContext.h"
#include "Combat/FGameDamageResult.h"

#include "GameGameplay.h"
#include "FGameGameplayEffectContext.generated.h"

/**
 * @brief 打击反馈上下文（游戏无关，100% 泛型与 Tag 驱动）
 */
USTRUCT()
struct GAMEGAMEPLAY_API FGameGameplayEffectContext : public FGameplayEffectContext {
    GENERATED_BODY()

public:
    /** 单次复制允许携带的通道 / 动态数值条目上限（计数按 uint8 传输，超出部分截断） */
    static constexpr int32 MaxNetSerializedEntries = 255;

    FGameGameplayEffectContext() = default;

    /**
     * @brief 获取实际生效的总伤害数值
     * @return 实际总伤害
     */
    [[nodiscard]] float GetActualDamage() const noexcept;

    /**
     * @brief 写入实际生效的总伤害数值
     * @param InActualDamage 实际生效总伤害
     */
    void SetActualDamage(float InActualDamage) noexcept;

    /**
     * @brief 获取指定通道实际结算伤害数值
     * @param ChannelTag 通道标签
     * @return 实际伤害数值，未匹配返回 0.0f
     */
    [[nodiscard]] float GetActualChannelDamage(const FGameplayTag& ChannelTag) const noexcept;

    /**
     * @brief 写入指定通道实际结算伤害数值
     * @param ChannelTag 通道标签
     * @param Magnitude 实际伤害数值
     */
    void SetActualChannelDamage(const FGameplayTag& ChannelTag, float Magnitude);

    /**
     * @brief 获取所有通道实际伤害明细
     * @return 通道数值列表
     */
    [[nodiscard]] const TArray<FGameDamageChannel>& GetActualChannels() const noexcept;

    /**
     * @brief 查询是否包含特定反馈标签（如命中、致死、暴击、硬直、格挡等）
     * @param InTag 待检测的反馈标签
     * @return 存在或其父标签匹配返回 true
     */
    [[nodiscard]] bool HasFeedbackTag(const FGameplayTag& InTag) const noexcept;

    /**
     * @brief 添加反馈标签
     * @param InTag 待添加的反馈标签
     */
    void AddFeedbackTag(const FGameplayTag& InTag) noexcept;

    /**
     * @brief 移除反馈标签
     * @param InTag 待移除的反馈标签
     */
    void RemoveFeedbackTag(const FGameplayTag& InTag) noexcept;

    /**
     * @brief 获取所有反馈标签
     * @return 反馈标签容器
     */
    [[nodiscard]] const FGameplayTagContainer& GetFeedbackTags() const noexcept;

    /**
     * @brief 获取动态数值参数（如卡肉时长、削韧值、护盾吸收量等）
     * @param InTag 数值属性标签
     * @param DefaultValue 未找到时的默认值
     * @return 属性数值
     */
    [[nodiscard]] float GetDynamicValue(const FGameplayTag& InTag, float DefaultValue = 0.0f) const noexcept;

    /**
     * @brief 设置或覆盖动态数值参数
     * @param InTag 数值属性标签
     * @param InValue 属性数值
     */
    void SetDynamicValue(const FGameplayTag& InTag, float InValue);

    /**
     * @brief 查询是否存在指定动态数值参数
     * @param InTag 数值属性标签
     * @return 存在返回 true
     */
    [[nodiscard]] bool HasDynamicValue(const FGameplayTag& InTag) const noexcept;

    /**
     * @brief 获取所有动态数值参数列表
     * @return 动态数值列表
     */
    [[nodiscard]] const TArray<FGameDamageChannel>& GetDynamicValues() const noexcept;

    /**
     * @brief 转换为战斗框架统一打击反馈契约
     * @return 统一打击反馈
     */
    [[nodiscard]] FGameDamageResult ToDamageResult() const;

    UScriptStruct* GetScriptStruct() const override;

    FGameplayEffectContext* Duplicate() const override;

    bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;

    /**
     * @brief 从上下文句柄中提取本工程上下文（类型不符或句柄为空返回 nullptr）
     * @param InHandle 上下文句柄
     * @return 本工程上下文指针，不匹配返回 nullptr
     */
    [[nodiscard]] static FGameGameplayEffectContext* ExtractEffectContext(const FGameplayEffectContextHandle& InHandle);

private:
    /** 统一打击反馈载荷 */
    UPROPERTY()
    FGameDamageResult DamageResult;
};

template<>
struct TStructOpsTypeTraits<FGameGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FGameGameplayEffectContext> {
    enum {
        WithNetSerializer = true,
        WithCopy = true,
    };
};
