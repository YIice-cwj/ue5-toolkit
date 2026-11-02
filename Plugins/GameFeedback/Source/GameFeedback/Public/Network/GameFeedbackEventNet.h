/**
 * @file GameFeedbackEventNet.h
 * @brief 网络紧凑打击表现载荷定义
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Math/Float16.h"
#include "Feedback/GameFeedbackSignal.h"

#include "GameFeedbackEventNet.generated.h"

/**
 * @brief 网络紧凑打击表现载荷（典型 <= 64 字节，完整 <= 96 字节）
 */
USTRUCT()
struct GAMEFEEDBACK_API FGameFeedbackEventNet {
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<AActor> Instigator = nullptr;

    UPROPERTY()
    TObjectPtr<AActor> DamageCauser = nullptr;

    UPROPERTY()
    TObjectPtr<AActor> Target = nullptr;

    UPROPERTY()
    TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

    UPROPERTY()
    FVector_NetQuantize ImpactLocation = FVector::ZeroVector;

    UPROPERTY()
    FVector_NetQuantizeNormal ImpactNormal = FVector::UpVector;

    UPROPERTY()
    FVector_NetQuantizeNormal ImpactDirection = FVector::ForwardVector;

    UPROPERTY()
    FVector_NetQuantizeNormal TraceDirection = FVector::ForwardVector;

    UPROPERTY()
    uint32 DefinitionId = 0;

    UPROPERTY()
    uint32 DefinitionMappingVersion = 0;

    /**
     * @brief 主通道网络索引；未启用 GameplayTag FastReplication 时为 MAX_uint16 哨兵
     */
    UPROPERTY()
    uint16 PrimaryChannelIndex = 0;

    UPROPERTY()
    uint16 DamageHalf = 0;

    UPROPERTY()
    uint8 StateFlags = 0;

    UPROPERTY()
    uint16 PredictionEventId = 0;

    UPROPERTY()
    uint16 BoneIndex = MAX_uint16;

    /**
     * @brief 写入伤害数值（转为半精度浮点编码）
     * @param InDamage 原始单精度伤害浮点数
     */
    FORCEINLINE void SetDamage(float InDamage) noexcept {
        DamageHalf = FFloat16(InDamage).Encoded;
    }

    /**
     * @brief 获取伤害数值（从半精度浮点解码）
     * @return 还原后的单精度伤害浮点数
     */
    [[nodiscard]] FORCEINLINE float GetDamage() const noexcept {
        FFloat16 Val;
        Val.Encoded = DamageHalf;
        return Val.GetFloat();
    }

    /**
     * @brief 自定义网络序列化（对象映射、定义身份、方向与骨骼紧凑编码）
     * @param Ar 序列化归档
     * @param Map 网络对象包映射器
     * @param bOutSuccess 序列化输出成功状态
     * @return 序列化是否成功
     */
    bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

    /**
     * @brief 从表现信号与服务端最终定义身份构造网络紧凑载荷
     * @param Signal 输入表现信号
     * @param DefinitionId 服务端最终 Definition 稳定 ID
     * @param DefinitionMappingVersion 服务端 Registry 映射版本
     * @return 网络紧凑载荷
     */
    static FGameFeedbackEventNet FromSignal(const FGameFeedbackSignal& Signal, uint32 DefinitionId, uint32 DefinitionMappingVersion);

    /**
     * @brief 将网络紧凑载荷还原为表现信号
     * @param NetEvent 网络紧凑载荷
     * @return 还原后的表现信号
     */
    static FGameFeedbackSignal ToSignal(const FGameFeedbackEventNet& NetEvent);
};

template <>
struct TStructOpsTypeTraits<FGameFeedbackEventNet> : public TStructOpsTypeTraitsBase2<FGameFeedbackEventNet> {
    enum {
        WithNetSerializer = true
    };
};

