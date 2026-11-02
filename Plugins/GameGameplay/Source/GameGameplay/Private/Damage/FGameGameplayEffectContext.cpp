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

float FGameGameplayEffectContext::GetActualDamage() const noexcept {
    return DamageResult.TotalActualDamage;
}

void FGameGameplayEffectContext::SetActualDamage(float InActualDamage) noexcept {
    DamageResult.SetActualDamage(InActualDamage);
}

float FGameGameplayEffectContext::GetActualChannelDamage(const FGameplayTag& ChannelTag) const noexcept {
    return DamageResult.GetActualChannelDamageExact(ChannelTag);
}

void FGameGameplayEffectContext::SetActualChannelDamage(const FGameplayTag& ChannelTag, float Magnitude) {
    FGameDamageChannel::SetExact(DamageResult.ActualChannels, ChannelTag, Magnitude);
}

const TArray<FGameDamageChannel>& FGameGameplayEffectContext::GetActualChannels() const noexcept {
    return DamageResult.ActualChannels;
}

bool FGameGameplayEffectContext::HasFeedbackTag(const FGameplayTag& InTag) const noexcept {
    return DamageResult.HasFeedbackTag(InTag);
}

void FGameGameplayEffectContext::AddFeedbackTag(const FGameplayTag& InTag) noexcept {
    DamageResult.AddFeedbackTag(InTag);
}

void FGameGameplayEffectContext::RemoveFeedbackTag(const FGameplayTag& InTag) noexcept {
    DamageResult.RemoveFeedbackTag(InTag);
}

const FGameplayTagContainer& FGameGameplayEffectContext::GetFeedbackTags() const noexcept {
    return DamageResult.FeedbackTags;
}

float FGameGameplayEffectContext::GetDynamicValue(const FGameplayTag& InTag, float DefaultValue) const noexcept {
    return DamageResult.GetDynamicValue(InTag, DefaultValue);
}

void FGameGameplayEffectContext::SetDynamicValue(const FGameplayTag& InTag, float InValue) {
    DamageResult.SetDynamicValue(InTag, InValue);
}

bool FGameGameplayEffectContext::HasDynamicValue(const FGameplayTag& InTag) const noexcept {
    return DamageResult.HasDynamicValue(InTag);
}

const TArray<FGameDamageChannel>& FGameGameplayEffectContext::GetDynamicValues() const noexcept {
    return DamageResult.DynamicValues;
}

FGameDamageResult FGameGameplayEffectContext::ToDamageResult() const {
    return DamageResult;
}

uint16 FGameGameplayEffectContext::GetFeedbackEventId() const noexcept {
    return FeedbackEventId;
}

void FGameGameplayEffectContext::SetFeedbackEventId(uint16 InFeedbackEventId) noexcept {
    FeedbackEventId = InFeedbackEventId;
}

UScriptStruct* FGameGameplayEffectContext::GetScriptStruct() const {
    return FGameGameplayEffectContext::StaticStruct();
}

FGameplayEffectContext* FGameGameplayEffectContext::Duplicate() const {
    FGameGameplayEffectContext* NewContext = new FGameGameplayEffectContext();
    *NewContext = *this;
    if (GetHitResult() != nullptr) {
        NewContext->AddHitResult(*GetHitResult(), true);
    }
    return NewContext;
}

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

    bOutSuccess = !Ar.IsError();
    return bOutSuccess;
}

FGameGameplayEffectContext* FGameGameplayEffectContext::ExtractEffectContext(const FGameplayEffectContextHandle& InHandle) {
    const FGameplayEffectContext* RawContext = InHandle.Get();
    if (RawContext == nullptr || RawContext->GetScriptStruct() != FGameGameplayEffectContext::StaticStruct()) {
        return nullptr;
    }
    return const_cast<FGameGameplayEffectContext*>(static_cast<const FGameGameplayEffectContext*>(RawContext));
}
