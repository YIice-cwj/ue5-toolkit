// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/GameFeedbackEventNet.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameplayTagsManager.h"
#include "System/GameFeedbackInternalHelpers.h"

/**
 * @brief 自定义网络序列化（对象映射、定义身份、方向与骨骼紧凑编码）
 * @param Ar 序列化归档
 * @param Map 网络对象包映射器
 * @param bOutSuccess 序列化输出成功状态
 * @return 序列化是否成功
 */
bool FGameFeedbackEventNet::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) {
    if (!Map) {
        bOutSuccess = false;
        return false;
    }

    bool bAllSuccess = true;

    UObject* RawInstigator = Instigator.Get();
    bAllSuccess &= Map->SerializeObject(Ar, AActor::StaticClass(), RawInstigator);
    Instigator = Cast<AActor>(RawInstigator);

    UObject* RawDamageCauser = DamageCauser.Get();
    bAllSuccess &= Map->SerializeObject(Ar, AActor::StaticClass(), RawDamageCauser);
    DamageCauser = Cast<AActor>(RawDamageCauser);

    UObject* RawTarget = Target.Get();
    bAllSuccess &= Map->SerializeObject(Ar, AActor::StaticClass(), RawTarget);
    Target = Cast<AActor>(RawTarget);

    UObject* RawHitComponent = HitComponent.Get();
    bAllSuccess &= Map->SerializeObject(Ar, UPrimitiveComponent::StaticClass(), RawHitComponent);
    HitComponent = Cast<UPrimitiveComponent>(RawHitComponent);

    bool bStepSuccess = true;
    ImpactLocation.NetSerialize(Ar, Map, bStepSuccess);
    bAllSuccess &= bStepSuccess;

    bStepSuccess = true;
    ImpactNormal.NetSerialize(Ar, Map, bStepSuccess);
    bAllSuccess &= bStepSuccess;

    bStepSuccess = true;
    ImpactDirection.NetSerialize(Ar, Map, bStepSuccess);
    bAllSuccess &= bStepSuccess;

    bStepSuccess = true;
    TraceDirection.NetSerialize(Ar, Map, bStepSuccess);
    bAllSuccess &= bStepSuccess;

    Ar << DefinitionId;
    Ar << DefinitionMappingVersion;
    Ar << PrimaryChannelIndex;
    Ar << DamageHalf;
    Ar << StateFlags;
    Ar << PredictionEventId;
    Ar << BoneIndex;

    bAllSuccess &= !Ar.IsError();
    bOutSuccess = bAllSuccess;
    return bOutSuccess;
}

/**
 * @brief 从表现信号与服务端最终定义身份构造网络紧凑载荷
 * @param Signal 输入表现信号
 * @param DefinitionId 服务端最终 Definition 稳定 ID
 * @param DefinitionMappingVersion 服务端 Registry 映射版本
 * @return 网络紧凑载荷
 *
 * 仅在工程启用 GameplayTag FastReplication 且主通道合法非 InvalidTagNetIndex 时写入网络索引；
 * 其余情况统一归一化为 MAX_uint16 哨兵，避免跨机 InvalidTagNetIndex 不一致。
 */
FGameFeedbackEventNet FGameFeedbackEventNet::FromSignal(
    const FGameFeedbackSignal& Signal,
    uint32 DefinitionId,
    uint32 DefinitionMappingVersion) {
    FGameFeedbackEventNet NetEvent;
    NetEvent.Instigator = Signal.Instigator.Get();
    NetEvent.DamageCauser = Signal.DamageCauser.Get();
    NetEvent.Target = Signal.Target.Get();
    NetEvent.HitComponent = Signal.HitResult.GetComponent();

    NetEvent.ImpactLocation = GameFeedback::Internal::ResolveImpactLocation(Signal);
    NetEvent.ImpactNormal = Signal.HitResult.ImpactNormal;
    NetEvent.ImpactDirection = Signal.ImpactDirection;
    NetEvent.TraceDirection = Signal.TraceDirection;
    NetEvent.DefinitionId = DefinitionId;
    NetEvent.DefinitionMappingVersion = DefinitionMappingVersion;

    NetEvent.PrimaryChannelIndex = MAX_uint16;
    const UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
    if (Signal.PrimaryChannel.IsValid() && TagManager.ShouldUseFastReplication()) {
        const FGameplayTagNetIndex NetIndex = TagManager.GetNetIndexFromTag(Signal.PrimaryChannel);
        if (NetIndex != TagManager.GetInvalidTagNetIndex()) {
            NetEvent.PrimaryChannelIndex = NetIndex;
        }
    }

    NetEvent.SetDamage(Signal.Magnitude);

    TBitMask<EGameFeedbackNetFlag> Flags;
    if (Signal.bLethal) {
        Flags.Set(EGameFeedbackNetFlag::Lethal);
    }
    NetEvent.SetFlags(Flags);
    NetEvent.PredictionEventId = Signal.PredictionEventId;

    if (!Signal.HitResult.BoneName.IsNone()) {
        if (const USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(Signal.HitResult.GetComponent())) {
            const int32 FoundBoneIndex = SkeletalComponent->GetBoneIndex(Signal.HitResult.BoneName);
            if (FoundBoneIndex >= 0 && FoundBoneIndex < MAX_uint16) {
                NetEvent.BoneIndex = static_cast<uint16>(FoundBoneIndex);
            }
        }
    }

    return NetEvent;
}

/**
 * @brief 将网络紧凑载荷还原为表现信号
 * @param NetEvent 网络紧凑载荷
 * @return 还原后的表现信号
 *
 * 仅在客户端启用 FastReplication 且主通道网络索引处于有效表范围内时还原 PrimaryChannel；
 * 哨兵或越界索引静默忽略而不触发引擎 ensure 断言，客户端依赖 DefinitionId 还原表现。
 */
FGameFeedbackSignal FGameFeedbackEventNet::ToSignal(const FGameFeedbackEventNet& NetEvent) {
    FGameFeedbackSignal Signal;
    Signal.Instigator = NetEvent.Instigator;
    Signal.DamageCauser = NetEvent.DamageCauser;
    Signal.Target = NetEvent.Target;
    Signal.HitResult.Location = NetEvent.ImpactLocation;
    Signal.HitResult.ImpactPoint = NetEvent.ImpactLocation;
    Signal.HitResult.Normal = NetEvent.ImpactNormal;
    Signal.HitResult.ImpactNormal = NetEvent.ImpactNormal;
    Signal.HitResult.Component = NetEvent.HitComponent;
    Signal.ImpactDirection = NetEvent.ImpactDirection;
    Signal.TraceDirection = NetEvent.TraceDirection;
    Signal.Magnitude = NetEvent.GetDamage();
    Signal.bLethal = NetEvent.GetFlags().Has(EGameFeedbackNetFlag::Lethal);
    Signal.PredictionEventId = NetEvent.PredictionEventId;

    if (const USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(NetEvent.HitComponent.Get())) {
        if (NetEvent.BoneIndex != MAX_uint16 && NetEvent.BoneIndex < SkeletalComponent->GetNumBones()) {
            Signal.HitResult.BoneName = SkeletalComponent->GetBoneName(NetEvent.BoneIndex);
        }
    }

    if (NetEvent.PrimaryChannelIndex != MAX_uint16) {
        UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
        if (TagManager.ShouldUseFastReplication()
            && static_cast<int32>(NetEvent.PrimaryChannelIndex) < TagManager.GetNetworkGameplayTagNodeIndex().Num()) {
            const FName PrimaryName = TagManager.GetTagNameFromNetIndex(NetEvent.PrimaryChannelIndex);
            if (!PrimaryName.IsNone()) {
                Signal.PrimaryChannel = TagManager.RequestGameplayTag(PrimaryName, false);
            }
        }
    }

    return Signal;
}
