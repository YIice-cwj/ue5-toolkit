/**
 * @file FGameGameplayEffectContext.h
 * @brief 打击反馈 GameplayEffect 上下文（全 Tag 驱动 · 泛型打击反馈契约）
 *
 * 设计思路：彻底消除对致死、卡肉等特定机制的硬编码字段假设；
 * 对齐 FGameDamageResult 架构，内置 TotalActualDamage、ActualChannels、FeedbackTags 与 DynamicValues；
 * 一切状态（命中/致死/暴击/格挡/硬直）均由 GameplayTag 驱动，一切数值（卡肉/削韧/护盾抵扣）均由动态通道承载；
 * 与引擎 FGameplayEffectContext 深度整合，支持 5-bit 位掩码紧凑 NetSerialize 与 Duplicate 深度复制。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Damage/FGameDamageResult.h"

#include "FGameGameplayEffectContext.generated.h"

/**
 * @brief 打击反馈上下文（游戏无关，100% 泛型与 Tag 驱动）
 */
USTRUCT()
struct GAMEGAMEPLAY_API FGameGameplayEffectContext : public FGameplayEffectContext {
    GENERATED_BODY()

public:
    /**
     * @brief 单次网络复制允许携带的通道 / 动态数值条目上限（计数按 uint8 传输，超出部分截断）
     */
    static constexpr int32 MaxNetSerializedEntries = 255;

    FGameGameplayEffectContext() = default;

    /**
     * @brief 从上下文句柄中提取本工程上下文（类型不符或句柄为空返回 nullptr）
     * @param InHandle 上下文句柄
     * @return 本工程上下文指针，不匹配返回 nullptr
     */
    [[nodiscard]] static FGameGameplayEffectContext* ExtractEffectContext(const FGameplayEffectContextHandle& InHandle);

    /**
     * @brief 获取实际生效的总伤害数值
     * @return 实际总伤害
     */
    [[nodiscard]] FORCEINLINE float GetActualDamage() const noexcept {
        return DamageResult.TotalActualDamage;
    }

    /**
     * @brief 写入实际生效的总伤害数值
     * @param InActualDamage 实际生效总伤害
     */
    FORCEINLINE void SetActualDamage(float InActualDamage) noexcept {
        DamageResult.SetActualDamage(InActualDamage);
    }

    /**
     * @brief 获取指定通道实际结算伤害数值
     * @param ChannelTag 通道标签
     * @return 实际伤害数值，未匹配返回 0.0f
     */
    [[nodiscard]] FORCEINLINE float GetActualChannelDamage(const FGameplayTag& ChannelTag) const noexcept {
        return DamageResult.GetActualChannelDamageExact(ChannelTag);
    }

    /**
     * @brief 写入指定通道实际结算伤害数值
     * @param ChannelTag 通道标签
     * @param Magnitude 实际伤害数值
     */
    FORCEINLINE void SetActualChannelDamage(const FGameplayTag& ChannelTag, float Magnitude) {
        DamageResult.SetActualChannelDamage(ChannelTag, Magnitude);
    }

    /**
     * @brief 获取所有通道实际伤害明细
     * @return 通道数值列表
     */
    [[nodiscard]] FORCEINLINE const TArray<FGameDamageChannel>& GetActualChannels() const noexcept {
        return DamageResult.ActualChannels;
    }

    /**
     * @brief 查询是否包含特定反馈标签（如命中、致死、暴击、硬直、格挡等）
     * @param InTag 待检测的反馈标签
     * @return 存在或其父标签匹配返回 true
     */
    [[nodiscard]] FORCEINLINE bool HasFeedbackTag(const FGameplayTag& InTag) const noexcept {
        return DamageResult.HasFeedbackTag(InTag);
    }

    /**
     * @brief 添加反馈标签
     * @param InTag 待添加的反馈标签
     */
    FORCEINLINE void AddFeedbackTag(const FGameplayTag& InTag) {
        DamageResult.AddFeedbackTag(InTag);
    }

    /**
     * @brief 移除反馈标签
     * @param InTag 待移除的反馈标签
     */
    FORCEINLINE void RemoveFeedbackTag(const FGameplayTag& InTag) {
        DamageResult.RemoveFeedbackTag(InTag);
    }

    /**
     * @brief 获取所有反馈标签
     * @return 反馈标签容器
     */
    [[nodiscard]] FORCEINLINE const FGameplayTagContainer& GetFeedbackTags() const noexcept {
        return DamageResult.FeedbackTags;
    }

    /**
     * @brief 获取动态数值参数（如卡肉时长、削韧值、护盾吸收量等）
     * @param InTag 数值属性标签
     * @param DefaultValue 未找到时的默认值
     * @return 属性数值
     */
    [[nodiscard]] FORCEINLINE float GetDynamicValue(const FGameplayTag& InTag, float DefaultValue = 0.0f) const noexcept {
        return DamageResult.GetDynamicValue(InTag, DefaultValue);
    }

    /**
     * @brief 设置或覆盖动态数值参数
     * @param InTag 数值属性标签
     * @param InValue 属性数值
     */
    FORCEINLINE void SetDynamicValue(const FGameplayTag& InTag, float InValue) {
        DamageResult.SetDynamicValue(InTag, InValue);
    }

    /**
     * @brief 查询是否存在指定动态数值参数
     * @param InTag 数值属性标签
     * @return 存在返回 true
     */
    [[nodiscard]] FORCEINLINE bool HasDynamicValue(const FGameplayTag& InTag) const noexcept {
        return DamageResult.HasDynamicValue(InTag);
    }

    /**
     * @brief 获取所有动态数值参数列表
     * @return 动态数值列表
     */
    [[nodiscard]] FORCEINLINE const TArray<FGameDamageChannel>& GetDynamicValues() const noexcept {
        return DamageResult.DynamicValues;
    }

    /**
     * @brief 转换为战斗框架统一打击反馈契约（返回常引用，消除整张结构体深拷贝）
     * @return 统一打击反馈常引用
     */
    [[nodiscard]] FORCEINLINE const FGameDamageResult& ToDamageResult() const noexcept {
        return DamageResult;
    }

    /**
     * @brief 获取跨端打击反馈事件 ID
     * @return 非零预测事件 ID；0 表示无预测身份
     */
    [[nodiscard]] FORCEINLINE uint16 GetFeedbackEventId() const noexcept {
        return FeedbackEventId;
    }

    /**
     * @brief 写入跨端打击反馈事件 ID
     * @param InFeedbackEventId 非零预测事件 ID；0 表示无预测身份
     */
    FORCEINLINE void SetFeedbackEventId(uint16 InFeedbackEventId) noexcept {
        FeedbackEventId = InFeedbackEventId;
    }

    /**
     * @brief 获取脚本结构（类型识别与上下文提取的类型依据）
     * @return 本上下文的结构体反射信息
     */
    UScriptStruct* GetScriptStruct() const override;

    /**
     * @brief 深复制上下文（含打击反馈载荷，命中结果按深拷贝追加）
     * @return 新建的上下文实例（调用方负责删除）
     */
    FGameplayEffectContext* Duplicate() const override;

    /**
     * @brief 紧凑网络序列化：先走基类，再按 5-bit 位掩码可选传输事件 ID、总伤害、反馈标签、通道明细与动态数值
     * @param Ar 归档
     * @param Map 网络包映射
     * @param bOutSuccess 输出是否成功（对全部子项做逻辑与累积，防错误状态被覆盖）
     * @return 序列化是否成功
     */
    bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;

private:
    /**
     * @brief 统一打击反馈载荷（记录各通道实际伤害、反馈标签与动态扩展数值）
     */
    UPROPERTY()
    FGameDamageResult DamageResult;

    /**
     * @brief 跨端预测打击反馈事件指纹 ID（0 表示无预测身份）
     */
    UPROPERTY()
    uint16 FeedbackEventId = 0;
};

template<>
struct TStructOpsTypeTraits<FGameGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FGameGameplayEffectContext> {
    enum {
        WithNetSerializer = true,
        WithCopy = true,
    };
};
