// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/PooledActorResetter.h"

#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "ObjectPool/IGamePoolable.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"

/**
 * @brief 根据指定的重置位掩码深度重置 Actor 的所有子组件与引擎状态
 * @param Actor 待重置的 Actor 实例
 * @param ResetFlags 重置策略位掩码
 */
void FPooledActorResetter::ResetActorComponents(AActor* Actor, EPoolResetFlags ResetFlags) {
    if (Actor == nullptr || ResetFlags == EPoolResetFlags::None) {
        return;
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Timers)) {
        Actor->GetWorldTimerManager().ClearAllTimersForObject(Actor);
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Physics)) {
        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent())) {
            RootPrim->SetPhysicsLinearVelocity(FVector::ZeroVector);
            RootPrim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Movement)) {
        if (UMovementComponent* MovementComp = Actor->FindComponentByClass<UMovementComponent>()) {
            MovementComp->StopMovementImmediately();
        }
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Audio)) {
        TInlineComponentArray<UAudioComponent*> AudioComponents(Actor);
        for (UAudioComponent* AudioComp : AudioComponents) {
            if (AudioComp == nullptr || !AudioComp->IsPlaying()) {
                continue;
            }
            AudioComp->Stop();
        }
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::FXSystem)) {
        TInlineComponentArray<UFXSystemComponent*> FXComponents(Actor);
        for (UFXSystemComponent* FXComp : FXComponents) {
            if (FXComp == nullptr || !FXComp->IsActive()) {
                continue;
            }
            FXComp->DeactivateImmediate();
            if (FXComp->IsActive()) {
                FXComp->Deactivate();
            }
        }
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Animation)) {
        TInlineComponentArray<USkeletalMeshComponent*> SkelComponents(Actor);
        for (USkeletalMeshComponent* SkelComp : SkelComponents) {
            if (SkelComp == nullptr) {
                continue;
            }
            SkelComp->Stop();
            SkelComp->ClearAnimScriptInstance();
            SkelComp->InitAnim(true);
        }
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::PawnAndAI)) {
        if (APawn* Pawn = Cast<APawn>(Actor)) {
            if (AController* Controller = Pawn->GetController()) {
                Controller->UnPossess();
            }
        }
    }
}

/**
 * @brief 安全调用受管对象的池化接口生命周期回调
 * @param TargetObject 目标对象实例
 * @param bAcquire 若为 true 触发借出唤醒回调，若为 false 触发归还休眠回调
 */
void FPooledActorResetter::InvokePoolableCallback(UObject* TargetObject, bool bAcquire) {
    if (TargetObject == nullptr || !TargetObject->GetClass()->ImplementsInterface(UGamePoolable::StaticClass())) {
        return;
    }

    IGamePoolable* NativePoolable = static_cast<IGamePoolable*>(
        TargetObject->GetNativeInterfaceAddress(UGamePoolable::StaticClass())
    );
    const bool bUseNative = NativePoolable != nullptr && TargetObject->GetClass()->HasAnyClassFlags(CLASS_Native);

    if (bAcquire) {
        if (bUseNative) {
            NativePoolable->OnAcquireFromPool_Implementation();
        } else {
            IGamePoolable::Execute_OnAcquireFromPool(TargetObject);
        }
        return;
    }

    if (bUseNative) {
        NativePoolable->OnReleaseToPool_Implementation();
    } else {
        IGamePoolable::Execute_OnReleaseToPool(TargetObject);
    }
}
