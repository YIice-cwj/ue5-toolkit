// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/GameFeedbackEventNet.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/PackageMapClient.h"
#include "GameplayTagsManager.h"

/**
 * @brief 自定义网络序列化（对象映射、定义身份、方向与骨骼紧凑编码）
 * @param Ar 序列化归档
 * @param Map 网络对象包映射器
 * @param bOutSuccess 序列化输出成功状态
 * @return 序列化是否成功
 */
bool FGameFeedbackEventNet::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
    if (!Map) {
        bOutSuccess = false;
        return false;
    }

    bOutSuccess = true;

    UObject* RawInstigator = Instigator.Get();
    bOutSuccess &= Map->SerializeObject(Ar, AActor::StaticClass(), RawInstigator);
    Instigator = Cast<AActor>(RawInstigator);

    UObject* RawDamageCauser = DamageCauser.Get();
    bOutSuccess &= Map->SerializeObject(Ar, AActor::StaticClass(), RawDamageCauser);
    DamageCauser = Cast<AActor>(RawDamageCauser);

    UObject* RawTarget = Target.Get();
    bOutSuccess &= Map->SerializeObject(Ar, AActor::StaticClass(), RawTarget);
    Target = Cast<AActor>(RawTarget);

    UObject* RawHitComponent = HitComponent.Get();
    bOutSuccess &= Map->SerializeObject(Ar, UPrimitiveComponent::StaticClass(), RawHitComponent);
    HitComponent = Cast<UPrimitiveComponent>(RawHitComponent);

    bool bLocationSuccess = true;
    ImpactLocation.NetSerialize(Ar, Map, bLocationSuccess);
    bOutSuccess &= bLocationSuccess;

    bool bNormalSuccess = true;
    ImpactNormal.NetSerialize(Ar, Map, bNormalSuccess);
    bOutSuccess &= bNormalSuccess;

    bool bDirectionSuccess = true;
    ImpactDirection.NetSerialize(Ar, Map, bDirectionSuccess);
    bOutSuccess &= bDirectionSuccess;

    bool bTraceDirectionSuccess = true;
    TraceDirection.NetSerialize(Ar, Map, bTraceDirectionSuccess);
    bOutSuccess &= bTraceDirectionSuccess;

    Ar << DefinitionId;
    Ar << DefinitionMappingVersion;
    Ar << PrimaryChannelIndex;
    Ar << DamageHalf;
    Ar << StateFlags;
    Ar << PredictionEventId;
    Ar << BoneIndex;

    bOutSuccess &= !Ar.IsError();
    return bOutSuccess;
}

/**
 * @brief 从表现信号与服务端最终定义身份构造网络紧凑载荷
 * @param Signal 输入表现信号
 * @param DefinitionId 服务端最终 Definition 稳定 ID
 * @param DefinitionMappingVersion 服务端 Registry 映射版本
 * @return 网络紧凑载荷
 *
 * 仅在工程启用 GameplayTag FastReplication 时写入主通道网络索引；未启用时使用
 * MAX_uint16 哨兵，客户端由 DefinitionId 还原表现而不重建通道标签。
 */
FGameFeedbackEventNet FGameFeedbackEventNet::FromSignal(
    const FGameFeedbackSignal& Signal,
    uint32 DefinitionId,
    uint32 DefinitionMappingVersion)
{
    FGameFeedbackEventNet NetEvent;
    NetEvent.Instigator = Signal.Instigator.Get();
    NetEvent.DamageCauser = Signal.DamageCauser.Get();
    NetEvent.Target = Signal.Target.Get();
    NetEvent.HitComponent = Signal.HitResult.GetComponent();

    const FVector Origin = Signal.HitResult.ImpactPoint.IsNearlyZero()
        ? (Signal.Target.IsValid() ? Signal.Target->GetActorLocation() : FVector::ZeroVector)
        : Signal.HitResult.ImpactPoint;

    NetEvent.ImpactLocation = Origin;
    NetEvent.ImpactNormal = Signal.HitResult.ImpactNormal;
    NetEvent.ImpactDirection = Signal.ImpactDirection;
    NetEvent.TraceDirection = Signal.TraceDirection;
    NetEvent.DefinitionId = DefinitionId;
    NetEvent.DefinitionMappingVersion = DefinitionMappingVersion;
    const UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
    NetEvent.PrimaryChannelIndex = TagManager.ShouldUseFastReplication()
        ? TagManager.GetNetIndexFromTag(Signal.PrimaryChannel)
        : MAX_uint16;
    NetEvent.SetDamage(Signal.Magnitude);
    NetEvent.StateFlags = (Signal.bLethal ? 1 : 0);
    NetEvent.PredictionEventId = Signal.PredictionEventId;

    if (const USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(Signal.HitResult.GetComponent())) {
        const int32 BoneIndex = SkeletalComponent->GetBoneIndex(Signal.HitResult.BoneName);
        if (BoneIndex >= 0 && BoneIndex < MAX_uint16) {
            NetEvent.BoneIndex = static_cast<uint16>(BoneIndex);
        }
    }

    return NetEvent;
}

/**
 * @brief 将网络紧凑载荷还原为表现信号
 * @param NetEvent 网络紧凑载荷
 * @return 还原后的表现信号
 *
 * 仅在载荷携带有效主通道网络索引时还原 PrimaryChannel；哨兵表示服务端未启用
 * FastReplication，此时客户端依赖 DefinitionId 还原表现。
 */
FGameFeedbackSignal FGameFeedbackEventNet::ToSignal(const FGameFeedbackEventNet& NetEvent)
{
    FGameFeedbackSignal Signal;
    Signal.Instigator = NetEvent.Instigator;
    Signal.DamageCauser = NetEvent.DamageCauser;
    Signal.Target = NetEvent.Target;
    Signal.HitResult.ImpactPoint = NetEvent.ImpactLocation;
    Signal.HitResult.ImpactNormal = NetEvent.ImpactNormal;
    Signal.HitResult.Component = NetEvent.HitComponent;
    Signal.ImpactDirection = NetEvent.ImpactDirection;
    Signal.TraceDirection = NetEvent.TraceDirection;
    Signal.Magnitude = NetEvent.GetDamage();
    Signal.bLethal = (NetEvent.StateFlags & 1) != 0;
    Signal.PredictionEventId = NetEvent.PredictionEventId;

    if (const USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(NetEvent.HitComponent.Get())) {
        if (NetEvent.BoneIndex != MAX_uint16 && NetEvent.BoneIndex < SkeletalComponent->GetNumBones()) {
            Signal.HitResult.BoneName = SkeletalComponent->GetBoneName(NetEvent.BoneIndex);
        }
    }

    if (NetEvent.PrimaryChannelIndex != MAX_uint16) {
        UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
        const FName PrimaryName = TagManager.GetTagNameFromNetIndex(NetEvent.PrimaryChannelIndex);
        if (!PrimaryName.IsNone()) {
            Signal.PrimaryChannel = TagManager.RequestGameplayTag(PrimaryName, false);
        }
    }

    return Signal;
}
