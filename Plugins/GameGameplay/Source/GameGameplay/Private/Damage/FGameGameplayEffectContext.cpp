/**
 * @file FGameGameplayEffectContext.cpp
 * @brief 打击反馈 GameplayEffect 上下文实现（全 Tag 驱动）
 */

#include "Damage/FGameGameplayEffectContext.h"

#include "Logging/GameLogCategories.h"

namespace {

/**
 * @brief 按网络上限序列化伤害通道数组，并在加载端重建对应容器
 * @param Ar 读写归档
 * @param Map 网络包映射
 * @param bOutSuccess 序列化成功标记
 * @param Channels 待序列化的通道数组
 * @param Label 超限日志中的容器名称
 */
void SerializeChannelArray(
    FArchive& Ar,
    UPackageMap* Map,
    bool& bOutSuccess,
    TArray<FGameDamageChannel>& Channels,
    const TCHAR* Label
) {
    int32 LocalCount = Ar.IsLoading() ? 0 : FMath::Min(Channels.Num(), FGameGameplayEffectContext::MaxNetSerializedEntries);
    if (Ar.IsSaving() && Channels.Num() > FGameGameplayEffectContext::MaxNetSerializedEntries) {
        GAME_LOG(Warning, TEXT("【打击上下文】%s超出网络复制上限，已截断：%d -> %d"),
            Label, Channels.Num(), FGameGameplayEffectContext::MaxNetSerializedEntries);
    }
    uint8 NumEntries = static_cast<uint8>(LocalCount);
    bool bHasEntries = NumEntries > 0;
    Ar.SerializeBits(&bHasEntries, 1);
    if (bHasEntries) {
        Ar << NumEntries;
        if (Ar.IsLoading()) {
            LocalCount = NumEntries;
            Channels.SetNum(LocalCount);
        }
        for (int32 Index = 0; Index < LocalCount; ++Index) {
            Channels[Index].ChannelTag.NetSerialize(Ar, Map, bOutSuccess);
            Ar << Channels[Index].Magnitude;
        }
    } else if (Ar.IsLoading()) {
        Channels.Reset();
    }
}

}  // namespace

/**
 * @brief 获取实际生效的总伤害数值
 * @return 实际总伤害
 */
float FGameGameplayEffectContext::GetActualDamage() const noexcept {
    return DamageResult.TotalActualDamage;
}

/**
 * @brief 写入实际生效的总伤害数值
 * @param InActualDamage 实际生效总伤害
 */
void FGameGameplayEffectContext::SetActualDamage(float InActualDamage) noexcept {
    DamageResult.SetActualDamage(InActualDamage);
}

/**
 * @brief 获取指定通道实际结算伤害数值
 * @param ChannelTag 通道标签
 * @return 实际伤害数值，未匹配返回 0.0f
 */
float FGameGameplayEffectContext::GetActualChannelDamage(const FGameplayTag& ChannelTag) const noexcept {
    return DamageResult.GetActualChannelDamageExact(ChannelTag);
}

/**
 * @brief 写入指定通道实际结算伤害数值
 * @param ChannelTag 通道标签
 * @param Magnitude 实际伤害数值
 */
void FGameGameplayEffectContext::SetActualChannelDamage(const FGameplayTag& ChannelTag, float Magnitude) {
    FGameDamageChannel::SetExact(DamageResult.ActualChannels, ChannelTag, Magnitude);
}

/**
 * @brief 获取所有通道实际伤害明细
 * @return 通道数值列表
 */
const TArray<FGameDamageChannel>& FGameGameplayEffectContext::GetActualChannels() const noexcept {
    return DamageResult.ActualChannels;
}

/**
 * @brief 查询是否包含特定反馈标签
 * @param InTag 待检测的反馈标签
 * @return 存在或其父标签匹配返回 true
 */
bool FGameGameplayEffectContext::HasFeedbackTag(const FGameplayTag& InTag) const noexcept {
    return DamageResult.HasFeedbackTag(InTag);
}

/**
 * @brief 添加反馈标签
 * @param InTag 待添加的反馈标签
 */
void FGameGameplayEffectContext::AddFeedbackTag(const FGameplayTag& InTag) noexcept {
    DamageResult.AddFeedbackTag(InTag);
}

/**
 * @brief 移除反馈标签
 * @param InTag 待移除的反馈标签
 */
void FGameGameplayEffectContext::RemoveFeedbackTag(const FGameplayTag& InTag) noexcept {
    DamageResult.RemoveFeedbackTag(InTag);
}

/**
 * @brief 获取所有反馈标签
 * @return 反馈标签容器
 */
const FGameplayTagContainer& FGameGameplayEffectContext::GetFeedbackTags() const noexcept {
    return DamageResult.FeedbackTags;
}

/**
 * @brief 获取动态数值参数
 * @param InTag 数值属性标签
 * @param DefaultValue 未找到时的默认值
 * @return 属性数值
 */
float FGameGameplayEffectContext::GetDynamicValue(const FGameplayTag& InTag, float DefaultValue) const noexcept {
    return DamageResult.GetDynamicValue(InTag, DefaultValue);
}

/**
 * @brief 设置或覆盖动态数值参数
 * @param InTag 数值属性标签
 * @param InValue 属性数值
 */
void FGameGameplayEffectContext::SetDynamicValue(const FGameplayTag& InTag, float InValue) {
    DamageResult.SetDynamicValue(InTag, InValue);
}

/**
 * @brief 查询是否存在指定动态数值参数
 * @param InTag 数值属性标签
 * @return 存在返回 true
 */
bool FGameGameplayEffectContext::HasDynamicValue(const FGameplayTag& InTag) const noexcept {
    return DamageResult.HasDynamicValue(InTag);
}

/**
 * @brief 获取所有动态数值参数列表
 * @return 动态数值列表
 */
const TArray<FGameDamageChannel>& FGameGameplayEffectContext::GetDynamicValues() const noexcept {
    return DamageResult.DynamicValues;
}

/**
 * @brief 获取跨端打击反馈事件 ID
 * @return 非零预测事件 ID；0 表示无预测身份
 */
uint16 FGameGameplayEffectContext::GetFeedbackEventId() const noexcept {
    return FeedbackEventId;
}

/**
 * @brief 写入跨端打击反馈事件 ID
 * @param InFeedbackEventId 非零预测事件 ID；0 表示无预测身份
 */
void FGameGameplayEffectContext::SetFeedbackEventId(uint16 InFeedbackEventId) noexcept {
    FeedbackEventId = InFeedbackEventId;
}

/**
 * @brief 获取脚本结构（类型识别与上下文提取的类型依据）
 * @return 本上下文的结构体反射信息
 */
UScriptStruct* FGameGameplayEffectContext::GetScriptStruct() const {
    return FGameGameplayEffectContext::StaticStruct();
}

/**
 * @brief 深复制上下文（含打击反馈载荷，命中结果按深拷贝追加）
 * @return 新建的上下文实例（调用方负责删除）
 */
FGameplayEffectContext* FGameGameplayEffectContext::Duplicate() const {
    FGameGameplayEffectContext* NewContext = new FGameGameplayEffectContext();
    *NewContext = *this;
    if (GetHitResult() != nullptr) {
        NewContext->AddHitResult(*GetHitResult(), true);
    }
    return NewContext;
}

/**
 * @brief 网络序列化：传输事件 ID、总伤害、反馈标签、通道明细与动态数值
 * @param Ar 归档
 * @param Map 网络包映射
 * @param bOutSuccess 输出是否成功
 * @return 序列化是否成功
 */
bool FGameGameplayEffectContext::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) {
    Super::NetSerialize(Ar, Map, bOutSuccess);
    if (!bOutSuccess) {
        return false;
    }

    Ar << FeedbackEventId;

    bool bHasActualDamage = DamageResult.TotalActualDamage != 0.0f;
    Ar.SerializeBits(&bHasActualDamage, 1);
    if (bHasActualDamage) {
        Ar << DamageResult.TotalActualDamage;
    } else if (Ar.IsLoading()) {
        DamageResult.TotalActualDamage = 0.0f;
    }

    bool bHasFeedbackTags = !DamageResult.FeedbackTags.IsEmpty();
    Ar.SerializeBits(&bHasFeedbackTags, 1);
    if (bHasFeedbackTags) {
        DamageResult.FeedbackTags.NetSerialize(Ar, Map, bOutSuccess);
    } else if (Ar.IsLoading()) {
        DamageResult.FeedbackTags.Reset();
    }

    SerializeChannelArray(Ar, Map, bOutSuccess, DamageResult.ActualChannels, TEXT("通道明细"));
    SerializeChannelArray(Ar, Map, bOutSuccess, DamageResult.DynamicValues, TEXT("动态数值"));

    bOutSuccess = bOutSuccess && !Ar.IsError();
    return bOutSuccess;
}

/**
 * @brief 从上下文句柄中提取本工程上下文（类型不符或句柄为空返回 nullptr）
 * @param InHandle 上下文句柄
 * @return 本工程上下文指针，不匹配返回 nullptr
 */
FGameGameplayEffectContext* FGameGameplayEffectContext::ExtractEffectContext(const FGameplayEffectContextHandle& InHandle) {
    const FGameplayEffectContext* RawContext = InHandle.Get();
    if (RawContext == nullptr || RawContext->GetScriptStruct() == nullptr || !RawContext->GetScriptStruct()->IsChildOf(FGameGameplayEffectContext::StaticStruct())) {
        return nullptr;
    }
    return const_cast<FGameGameplayEffectContext*>(static_cast<const FGameGameplayEffectContext*>(RawContext));
}
