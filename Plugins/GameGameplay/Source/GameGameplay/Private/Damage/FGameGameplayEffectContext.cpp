/**
 * @file FGameGameplayEffectContext.cpp
 * @brief 打击反馈 GameplayEffect 上下文实现（全 Tag 驱动与 5-bit 紧凑网络序列化）
 */

#include "Damage/FGameGameplayEffectContext.h"

#include "GameGameplayLog.h"

namespace {

constexpr uint8 RepFlagFeedbackEventId = 1u << 0;
constexpr uint8 RepFlagActualDamage = 1u << 1;
constexpr uint8 RepFlagFeedbackTags = 1u << 2;
constexpr uint8 RepFlagActualChannels = 1u << 3;
constexpr uint8 RepFlagDynamicValues = 1u << 4;
constexpr uint32 RepFlagBitCount = 5u;
constexpr double TruncationLogThrottleSeconds = 5.0;

/**
 * @brief 按网络上限序列化伤害通道数组，并在加载端重建对应容器，累积子项成功状态
 * @param Ar 读写归档
 * @param Map 网络包映射
 * @param bHasEntries 位掩码指示该数组是否存在非空数据
 * @param Channels 待序列化的通道数组
 * @param Label 超限日志中的容器名称
 * @param bInOutSuccess 累积序列化成功标记
 */
void SerializeChannelArray(
    FArchive& Ar,
    UPackageMap* Map,
    bool bHasEntries,
    TArray<FGameDamageChannel>& Channels,
    const TCHAR* Label,
    bool& bInOutSuccess
) {
    if (!bHasEntries) {
        if (Ar.IsLoading()) {
            Channels.Reset();
        }
        return;
    }

    int32 LocalCount = 0;
    if (Ar.IsSaving()) {
        LocalCount = FMath::Min(Channels.Num(), FGameGameplayEffectContext::MaxNetSerializedEntries);
        if (Channels.Num() > FGameGameplayEffectContext::MaxNetSerializedEntries) {
            GAME_GAS_LOG_THROTTLE(
                LogGameDamage,
                TruncationLogThrottleSeconds,
                Warning,
                TEXT("【打击上下文】%s超出网络复制上限，已截断：%d -> %d。"),
                Label,
                Channels.Num(),
                FGameGameplayEffectContext::MaxNetSerializedEntries
            );
        }
    }

    uint8 NumEntries = static_cast<uint8>(LocalCount);
    Ar << NumEntries;
    if (Ar.IsError()) {
        bInOutSuccess = false;
        return;
    }

    if (Ar.IsLoading()) {
        LocalCount = NumEntries;
        Channels.SetNum(LocalCount);
    }

    for (int32 Index = 0; Index < LocalCount; ++Index) {
        bool bTagSuccess = true;
        Channels[Index].ChannelTag.NetSerialize(Ar, Map, bTagSuccess);
        Ar << Channels[Index].Magnitude;
        bInOutSuccess = bInOutSuccess && bTagSuccess && !Ar.IsError();
        if (Ar.IsError()) {
            if (Ar.IsLoading()) {
                Channels.Reset();
            }
            break;
        }
    }
}

}  // namespace

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
 * @brief 紧凑网络序列化：先走基类，再按 5-bit 位掩码可选传输事件 ID、总伤害、反馈标签、通道明细与动态数值
 * @param Ar 归档
 * @param Map 网络包映射
 * @param bOutSuccess 输出是否成功（对全部子项做逻辑与累积，防错误状态被覆盖）
 * @return 序列化是否成功
 */
bool FGameGameplayEffectContext::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) {
    bool bAllSuccess = true;
    Super::NetSerialize(Ar, Map, bAllSuccess);
    if (Ar.IsError()) {
        bOutSuccess = false;
        return false;
    }

    uint8 RepBits = 0;
    if (Ar.IsSaving()) {
        if (FeedbackEventId != 0) {
            RepBits |= RepFlagFeedbackEventId;
        }
        if (DamageResult.TotalActualDamage != 0.0f) {
            RepBits |= RepFlagActualDamage;
        }
        if (!DamageResult.FeedbackTags.IsEmpty()) {
            RepBits |= RepFlagFeedbackTags;
        }
        if (!DamageResult.ActualChannels.IsEmpty()) {
            RepBits |= RepFlagActualChannels;
        }
        if (!DamageResult.DynamicValues.IsEmpty()) {
            RepBits |= RepFlagDynamicValues;
        }
    }

    Ar.SerializeBits(&RepBits, RepFlagBitCount);
    if (Ar.IsError()) {
        bOutSuccess = false;
        return false;
    }

    if ((RepBits & RepFlagFeedbackEventId) != 0) {
        Ar << FeedbackEventId;
    } else if (Ar.IsLoading()) {
        FeedbackEventId = 0;
    }

    if ((RepBits & RepFlagActualDamage) != 0) {
        Ar << DamageResult.TotalActualDamage;
    } else if (Ar.IsLoading()) {
        DamageResult.TotalActualDamage = 0.0f;
    }

    if ((RepBits & RepFlagFeedbackTags) != 0) {
        bool bTagsSuccess = true;
        DamageResult.FeedbackTags.NetSerialize(Ar, Map, bTagsSuccess);
        bAllSuccess = bAllSuccess && bTagsSuccess;
    } else if (Ar.IsLoading()) {
        DamageResult.FeedbackTags.Reset();
    }

    SerializeChannelArray(Ar, Map, (RepBits & RepFlagActualChannels) != 0, DamageResult.ActualChannels, TEXT("通道明细"), bAllSuccess);
    SerializeChannelArray(Ar, Map, (RepBits & RepFlagDynamicValues) != 0, DamageResult.DynamicValues, TEXT("动态数值"), bAllSuccess);

    bOutSuccess = bAllSuccess && !Ar.IsError();
    return bOutSuccess;
}
