/**
 * @file FPooledActorResetter.cpp
 * @brief 池化 Actor 单次遍历深度组件重置与 IGamePoolable 生命周期回调派发实现
 */

#include "Managers/FPooledActorResetter.h"

#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IGamePoolable.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Particles/ParticleSystemComponent.h"
#include "Telemetry/ObjectPoolStats.h"
#include "TimerManager.h"

namespace {

constexpr EPoolResetFlags ComponentScanMask =
    EPoolResetFlags::Movement |
    EPoolResetFlags::Audio |
    EPoolResetFlags::FXSystem |
    EPoolResetFlags::Animation;

void ResetSingleComponent(UActorComponent* Component, EPoolResetFlags ResetFlags) {
    if (Component == nullptr) {
        return;
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Movement)) {
        if (UMovementComponent* MovementComp = Cast<UMovementComponent>(Component)) {
            MovementComp->StopMovementImmediately();
            return;
        }
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Audio)) {
        if (UAudioComponent* AudioComp = Cast<UAudioComponent>(Component)) {
            if (AudioComp->IsPlaying()) {
                AudioComp->Stop();
            }
            return;
        }
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::FXSystem)) {
        UFXSystemComponent* FXComp = Cast<UFXSystemComponent>(Component);
        if (FXComp != nullptr) {
            if (!FXComp->IsActive()) {
                return;
            }
            FXComp->DeactivateImmediate();
            if (FXComp->IsActive()) {
                FXComp->Deactivate();
            }
            return;
        }
    }

    if (!EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Animation)) {
        return;
    }
    USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component);
    if (SkelComp == nullptr) {
        return;
    }
    SkelComp->Stop();
    if (SkelComp->GetAnimInstance() != nullptr || SkelComp->GetSkeletalMeshAsset() != nullptr) {
        SkelComp->ClearAnimScriptInstance();
        SkelComp->InitAnim(true);
    }
}

}  // namespace

/**
 * @brief 按重置位掩码单次遍历并深度清洗 Actor 的所有子组件与引擎状态
 * @param Actor 待重置的 Actor 实例
 * @param ResetFlags 重置策略位掩码
 */
void FPooledActorResetter::ResetActorComponents(AActor* Actor, const EPoolResetFlags ResetFlags) {
    if (Actor == nullptr || ResetFlags == EPoolResetFlags::None) {
        return;
    }

    SCOPE_CYCLE_COUNTER(STAT_GamePool_ComponentReset);
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_ResetActorComponents);

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Timers)) {
        if (UWorld* World = Actor->GetWorld()) {
            World->GetTimerManager().ClearAllTimersForObject(Actor);
        }
    }

    if (EnumHasAnyFlags(ResetFlags, EPoolResetFlags::Physics)) {
        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent())) {
            if (RootPrim->IsSimulatingPhysics()) {
                RootPrim->SetPhysicsLinearVelocity(FVector::ZeroVector);
                RootPrim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            }
        }
    }

    if (EnumHasAnyFlags(ResetFlags, ComponentScanMask)) {
        for (UActorComponent* Component : Actor->GetComponents()) {
            ResetSingleComponent(Component, ResetFlags);
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
 * @brief 安全派发 IGamePoolable 的借出/归还生命周期回调（纯 C++ 走直调快路径，蓝图子类走 BlueprintNativeEvent 执行链）
 * @param TargetObject 目标对象实例
 * @param bAcquire 若为 true 触发借出唤醒回调，若为 false 触发归还休眠回调
 */
void FPooledActorResetter::InvokePoolableCallback(UObject* TargetObject, const bool bAcquire) {
    if (TargetObject == nullptr) {
        return;
    }

    const UClass* TargetClass = TargetObject->GetClass();
    if (TargetClass->IsNative()) {
        if (IGamePoolable* NativePoolable = Cast<IGamePoolable>(TargetObject)) {
            if (bAcquire) {
                NativePoolable->OnAcquireFromPool_Implementation();
            } else {
                NativePoolable->OnReleaseToPool_Implementation();
            }
        }
        return;
    }

    if (TargetClass->ImplementsInterface(UGamePoolable::StaticClass())) {
        if (bAcquire) {
            IGamePoolable::Execute_OnAcquireFromPool(TargetObject);
        } else {
            IGamePoolable::Execute_OnReleaseToPool(TargetObject);
        }
    }
}

/**
 * @brief 安全派发 IGamePoolable 的带载荷借出回调
 * @param TargetObject 目标对象实例
 * @param Payload 带编译期类型指纹的业务载荷视图
 */
void FPooledActorResetter::InvokePoolablePayloadCallback(UObject* TargetObject, const FPoolAcquirePayload Payload) {
    if (TargetObject == nullptr || !Payload.IsValid()) {
        return;
    }
    if (IGamePoolable* NativePoolable = Cast<IGamePoolable>(TargetObject)) {
        NativePoolable->OnAcquireFromPoolWithPayload(Payload);
    }
}
