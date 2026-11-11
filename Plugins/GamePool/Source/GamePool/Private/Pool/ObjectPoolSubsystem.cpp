// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pool/ObjectPoolSubsystem.h"

#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Config/GameCompileSwitches.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/MovementComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Pool/IGamePoolable.h"
#include "Async/FGameCoroAwaiters.h"
#include "Async/UTimeSlicedTaskSubsystem.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameObjectPool, Log, All);

namespace {

void DeactivateActorForPool(AActor* Actor) {
    Actor->SetActorHiddenInGame(true);
    Actor->SetActorEnableCollision(false);
    Actor->SetActorTickEnabled(false);
}

void InvokePoolableCallback(UObject* TargetObject, bool bAcquire) {
    if (!TargetObject->GetClass()->ImplementsInterface(UGamePoolable::StaticClass())) {
        return;
    }
    IGamePoolable* NativePoolable = static_cast<IGamePoolable*>(
        TargetObject->GetNativeInterfaceAddress(UGamePoolable::StaticClass()));
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

void ResetActorComponents(AActor* Actor, EPoolResetFlags ResetFlags) {
    if (ResetFlags == EPoolResetFlags::None) {
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
}

bool IsValidPoolableUObjectClass(const UClass* ObjectClass) {
    return ObjectClass != nullptr &&
        !ObjectClass->HasAnyClassFlags(CLASS_Abstract) &&
        !ObjectClass->IsChildOf(AActor::StaticClass());
}

}  // namespace

UObjectPoolSubsystem& UObjectPoolSubsystem::Get(const UObject* WorldContextObject) {
    UObjectPoolSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

UObjectPoolSubsystem* UObjectPoolSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (!WorldContextObject) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UObjectPoolSubsystem>();
    }
    return nullptr;
}

void UObjectPoolSubsystem::Deinitialize() {
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameObjectPool, Log, TEXT("【对象池】子系统注销，清空 %d 个 Actor 池与 %d 个 UObject 池。"),
            PoolMap.Num(), ObjectPoolMap.Num());
    }
    ClearAllPools();
    ObjectPoolMap.Empty();
    PoolMap.Empty();
    Super::Deinitialize();
}

void UObjectPoolSubsystem::Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount) {
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || PrewarmCount <= 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】预热失败：ActorClass 无效或预热数量 <= 0。"));
        }
        return;
    }

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        SpawnIdleActorToPool(ActorClass);
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameObjectPool, Log, TEXT("【对象池】预热完成: 类=[%s], 请求数量=%d, 当前空闲池大小=%d"),
            *ActorClass->GetName(), PrewarmCount, PoolMap.FindOrAdd(ActorClass).IdleActors.Num());
    }
}

FTimeSlicedTaskHandle UObjectPoolSubsystem::PrewarmTimeSliced(
    TSubclassOf<AActor> ActorClass,
    int32 PrewarmCount,
    int32 MaxSpawnsPerStep) {
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || PrewarmCount <= 0) {
        return FTimeSlicedTaskHandle();
    }

    UWorld* World = GetWorld();
    UTimeSlicedTaskSubsystem* TaskSubsystem = World != nullptr ? World->GetSubsystem<UTimeSlicedTaskSubsystem>() : nullptr;
    if (TaskSubsystem == nullptr) {
        Prewarm(ActorClass, PrewarmCount);
        return FTimeSlicedTaskHandle();
    }

    const int32 BatchSize = FMath::Max(1, MaxSpawnsPerStep);
    TSharedRef<int32> RemainingCount = MakeShared<int32>(PrewarmCount);
    TWeakObjectPtr<UObjectPoolSubsystem> WeakThis(this);

    return TaskSubsystem->EnqueueTask(this, [WeakThis, ActorClass, BatchSize, RemainingCount]() {
        UObjectPoolSubsystem* Self = WeakThis.Get();
        if (Self == nullptr || *RemainingCount <= 0) {
            return false;
        }

        const int32 StepTarget = FMath::Min(*RemainingCount, BatchSize);
        for (int32 StepIdx = 0; StepIdx < StepTarget; ++StepIdx) {
            const FPoolBucket& Bucket = Self->PoolMap.FindOrAdd(ActorClass);
            if (Bucket.MaxIdleCount > 0 && Bucket.IdleActors.Num() >= Bucket.MaxIdleCount) {
                *RemainingCount = 0;
                return false;
            }
            if (!Self->SpawnIdleActorToPool(ActorClass)) {
                *RemainingCount = 0;
                return false;
            }
            --(*RemainingCount);
        }
        return *RemainingCount > 0;
    });
}

/**
 * @brief C++20 协程分帧渐进预热 Actor 对象池（按帧预算让出，支持直接 co_await 与生命周期自毁熔断）
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 预热生成的空闲实例总数量
 * @param MaxSpawnsPerStep 单步最大生成实例数（默认每步 1 个）
 * @return 实际成功预热生成的实例总数异步协程任务
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmAsync(
    TSubclassOf<AActor> ActorClass,
    int32 PrewarmCount,
    int32 MaxSpawnsPerStep
) {
    co_await Game::Coro::BindContext(this);
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || PrewarmCount <= 0) {
        co_return 0;
    }

    int32 Total = 0;
    int32 Active = 0;
    int32 InitialIdle = 0;
    GetPoolStats(ActorClass, Total, Active, InitialIdle);

    FTimeSlicedTaskHandle Handle = PrewarmTimeSliced(ActorClass, PrewarmCount, MaxSpawnsPerStep);
    while (Handle.IsActive()) {
        co_await Game::Coro::YieldTimeSlice(this);
    }

    int32 FinalIdle = 0;
    GetPoolStats(ActorClass, Total, Active, FinalIdle);
    co_return FinalIdle - InitialIdle;
}

void UObjectPoolSubsystem::SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount) {
    if (!ActorClass) {
        return;
    }

    PoolMap.FindOrAdd(ActorClass).MaxIdleCount = InMaxIdleCount;
    while (true) {
        TObjectPtr<AActor> ExcessActor;
        {
            FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
            if (InMaxIdleCount <= 0 || Bucket.IdleActors.Num() <= InMaxIdleCount) {
                break;
            }
            ExcessActor = Bucket.IdleActors.Pop();
            Bucket.IdleActorSet.Remove(ExcessActor);
        }
        if (IsValid(ExcessActor.Get())) {
            ExcessActor->OnDestroyed.RemoveAll(this);
            ExcessActor->Destroy();
        }
    }
}

void UObjectPoolSubsystem::SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags) {
    if (!ActorClass) {
        return;
    }
    PoolMap.FindOrAdd(ActorClass).ResetFlags = InResetFlags;
}

EPoolResetFlags UObjectPoolSubsystem::GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const {
    if (!ActorClass) {
        return EPoolResetFlags::All;
    }
    if (const FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        return Bucket->ResetFlags;
    }
    return EPoolResetFlags::All;
}

/**
 * @brief 从池中借出或生成 Actor 实例（蓝图轻量门面）
 * @param ActorClass 目标类
 * @param SpawnTransform 初始空间变换
 * @return 激活的 Actor 实例指针；失败返回 nullptr
 */
AActor* UObjectPoolSubsystem::K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    const TGameStatusResult<AActor*> Result = AcquireActor<AActor>(ActorClass, SpawnTransform);
    return Result.IsSuccess() ? Result.GetValue() : nullptr;
}

/**
 * @brief 内部借出或生成 Actor 实例核心实现
 * @param ActorClass 目标类
 * @param SpawnTransform 初始空间变换
 * @return 成功返回 Actor 指针，失败返回 Failure
 */
TGameStatusResult<AActor*> UObjectPoolSubsystem::AcquireActorInternal(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    UWorld* World = GetWorld();
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || World == nullptr) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】借出 Actor 失败：ActorClass 或世界上下文无效。"));
        }
        return TGameStatusResult<AActor*>::Failure(5001, TEXT("ActorClass or World is invalid"));
    }

    AActor* ResultActor = nullptr;

    while (true) {
        TObjectPtr<AActor> Candidate;
        {
            FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
            if (Bucket.IdleActors.IsEmpty()) {
                break;
            }
            Candidate = Bucket.IdleActors.Pop();
            Bucket.IdleActorSet.Remove(Candidate);
        }
        if (IsValid(Candidate.Get())) {
            ResultActor = Candidate.Get();
            break;
        }
    }

    if (!ResultActor) {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ResultActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
    } else {
        ResultActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);
        if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(ResultActor->GetRootComponent())) {
            RootPrim->SetPhysicsLinearVelocity(FVector::ZeroVector);
            RootPrim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
    }

    if (!ResultActor) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Error, TEXT("【对象池】生成 Actor 实例失败：类=[%s]"), *ActorClass->GetName());
        }
        return TGameStatusResult<AActor*>::Failure(5002, TEXT("Failed to spawn Actor"));
    }

    const AActor* DefaultActor = ActorClass->GetDefaultObject<AActor>();
    const bool bDefaultCollision = DefaultActor->GetActorEnableCollision();
    const bool bDefaultTickEnabled = DefaultActor->PrimaryActorTick.bCanEverTick && DefaultActor->PrimaryActorTick.bStartWithTickEnabled;
    const float DefaultTimeDilation = DefaultActor->CustomTimeDilation;

    ResultActor->SetActorHiddenInGame(false);
    ResultActor->SetActorEnableCollision(bDefaultCollision);
    ResultActor->SetActorTickEnabled(bDefaultTickEnabled);
    ResultActor->CustomTimeDilation = DefaultTimeDilation;

    ResultActor->OnDestroyed.AddUniqueDynamic(this, &UObjectPoolSubsystem::OnPooledActorDestroyed);
    {
        FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
        Bucket.ActiveActorSet.Add(ResultActor);
        ++Bucket.ActiveCount;
    }

    InvokePoolableCallback(ResultActor, true);
    const FPoolBucket* Bucket = PoolMap.Find(ActorClass);
    if (!IsValid(ResultActor) || Bucket == nullptr || !Bucket->ActiveActorSet.Contains(ResultActor)) {
        return TGameStatusResult<AActor*>::Failure(5002, TEXT("Actor was invalidated during OnAcquireFromPool"));
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameObjectPool, Verbose, TEXT("【对象池】借出 Actor 成功: 类=[%s], 实例=[%s], 活跃数=%d, 剩余空闲数=%d"),
            *ActorClass->GetName(), *GetNameSafe(ResultActor), Bucket->ActiveCount, Bucket->IdleActors.Num());
    }
    return TGameStatusResult<AActor*>::Success(ResultActor);
}

/**
 * @brief 将已借出的 Actor 归还回池休眠（蓝图轻量门面）
 * @param Actor 待归还的实例
 */
void UObjectPoolSubsystem::K2_ReleaseActor(AActor* Actor) {
    ReleaseActor(Actor);
}

/**
 * @brief 将已借出的 Actor 归还回池休眠（强类型契约）
 * @param Actor 待归还的实例
 * @return 成功归还返回 Success，未借出或已销毁返回 Failure(5003/5004)
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseActor(AActor* Actor) {
    if (!IsValid(Actor)) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】归还 Actor 失败：Actor 无效。"));
        }
        return TGameStatusResult<void>::Failure(5004, TEXT("Actor is invalid or already destroyed"));
    }

    TSubclassOf<AActor> ActorClass = Actor->GetClass();
    FPoolBucket* Bucket = PoolMap.Find(ActorClass);
    if (Bucket == nullptr || Bucket->ActiveActorSet.Remove(Actor) == 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】归还警告：Actor [%s] 不是当前活跃池属实例，已忽略归还。"), *GetNameSafe(Actor));
        }
        return TGameStatusResult<void>::Failure(5003, TEXT("Actor is not an active member of this pool"));
    }
    --Bucket->ActiveCount;
    const EPoolResetFlags ResetFlags = Bucket->ResetFlags;

    Actor->OnDestroyed.RemoveAll(this);
    InvokePoolableCallback(Actor, false);
    if (!IsValid(Actor)) {
        return TGameStatusResult<void>::Success();
    }

    Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    TArray<AActor*> AttachedChildren;
    Actor->GetAttachedActors(AttachedChildren);
    for (AActor* ChildActor : AttachedChildren) {
        if (IsValid(ChildActor)) {
            ChildActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        }
    }

    Actor->CustomTimeDilation = 1.0f;

    ResetActorComponents(Actor, ResetFlags);

    if (!IsValid(Actor)) {
        return TGameStatusResult<void>::Success();
    }
    DeactivateActorForPool(Actor);

    bool bDestroyActor = false;
    {
        FPoolBucket& CurrentBucket = PoolMap.FindOrAdd(ActorClass);
        if (CurrentBucket.MaxIdleCount > 0 && CurrentBucket.IdleActors.Num() >= CurrentBucket.MaxIdleCount) {
            bDestroyActor = true;
        } else {
            CurrentBucket.IdleActors.Add(Actor);
            CurrentBucket.IdleActorSet.Add(Actor);
        }
    }
    if (bDestroyActor) {
        Actor->Destroy();
    }

    if constexpr (Game::Config::bEnableLog) {
        const FPoolBucket* CurrentBucket = PoolMap.Find(ActorClass);
        UE_LOG(LogGameObjectPool, Verbose, TEXT("【对象池】归还 Actor 成功: 类=[%s], 实例=[%s], 剩余活跃数=%d, 当前空闲数=%d"),
            *ActorClass->GetName(), *GetNameSafe(Actor), CurrentBucket->ActiveCount, CurrentBucket->IdleActors.Num());
    }
    return TGameStatusResult<void>::Success();
}

void UObjectPoolSubsystem::GetPoolStats(
    TSubclassOf<AActor> ActorClass,
    int32& OutTotalCount,
    int32& OutActiveCount,
    int32& OutIdleCount
) const {
    OutTotalCount = 0;
    OutActiveCount = 0;
    OutIdleCount = 0;

    if (const FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        OutIdleCount = Bucket->IdleActors.Num();
        OutActiveCount = Bucket->ActiveCount;
        OutTotalCount = OutIdleCount + OutActiveCount;
    }
}

void UObjectPoolSubsystem::ClearPool(TSubclassOf<AActor> ActorClass) {
    if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        const int32 Count = Bucket->IdleActors.Num();
        DestroyBucketIdleActors(*Bucket);
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Log, TEXT("【对象池】清理对象池: 类=[%s], 销毁 Actor 数=%d"), *ActorClass->GetName(), Count);
        }
    }
}

void UObjectPoolSubsystem::ClearAllPools() {
    TArray<TSubclassOf<AActor>> ActorClasses;
    PoolMap.GenerateKeyArray(ActorClasses);
    for (const TSubclassOf<AActor>& ActorClass : ActorClasses) {
        if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
            DestroyBucketIdleActors(*Bucket);
        }
    }
    ClearAllObjectPools();
}

void UObjectPoolSubsystem::PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount) {
    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass) || PrewarmCount <= 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】UObject 预热失败：类型无效（禁传抽象类或 AActor 派生类）或数量 <= 0。"));
        }
        return;
    }

    FObjectPoolBucket& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        if (Bucket.MaxIdleCount > 0 && Bucket.IdleObjects.Num() >= Bucket.MaxIdleCount) {
            break;
        }
        UObject* NewObj = NewObject<UObject>(this, RawClass);
        if (NewObj != nullptr) {
            Bucket.IdleObjects.Add(NewObj);
            Bucket.IdleObjectSet.Add(NewObj);
        }
    }
}

void UObjectPoolSubsystem::SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount) {
    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass)) {
        return;
    }

    FObjectPoolBucket& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
    Bucket.MaxIdleCount = InMaxIdleCount;
    while (InMaxIdleCount > 0 && Bucket.IdleObjects.Num() > InMaxIdleCount) {
        TObjectPtr<UObject> ExcessObject = Bucket.IdleObjects.Pop();
        Bucket.IdleObjectSet.Remove(ExcessObject);
        if (IsValid(ExcessObject.Get())) {
            ExcessObject->MarkAsGarbage();
        }
    }
}

/**
 * @brief 从池中借出或构建非 Actor UObject 实例（蓝图轻量门面）
 * @param ObjectClass 目标 UObject 派生类
 * @return 激活的 UObject 实例指针；失败返回 nullptr
 */
UObject* UObjectPoolSubsystem::K2_AcquireObject(TSubclassOf<UObject> ObjectClass) {
    const TGameStatusResult<UObject*> Result = AcquireObject<UObject>(ObjectClass);
    return Result.IsSuccess() ? Result.GetValue() : nullptr;
}

/**
 * @brief 内部借出或构建非 Actor UObject 实例核心实现
 * @param ObjectClass 目标 UObject 派生类
 * @return 成功返回 UObject 指针，失败返回 Failure
 */
TGameStatusResult<UObject*> UObjectPoolSubsystem::AcquireObjectInternal(TSubclassOf<UObject> ObjectClass) {
    UClass* RawClass = ObjectClass.Get();
    if (!IsValidPoolableUObjectClass(RawClass)) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】借出 UObject 失败：ObjectClass 无效或属于 AActor 派生类。"));
        }
        return TGameStatusResult<UObject*>::Failure(5001, TEXT("ObjectClass is invalid or is AActor subclass"));
    }

    UObject* ResultObject = nullptr;
    while (true) {
        TObjectPtr<UObject> Candidate;
        {
            FObjectPoolBucket& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
            if (Bucket.IdleObjects.IsEmpty()) {
                break;
            }
            Candidate = Bucket.IdleObjects.Pop();
            Bucket.IdleObjectSet.Remove(Candidate);
        }
        if (IsValid(Candidate.Get())) {
            ResultObject = Candidate.Get();
            break;
        }
    }

    if (ResultObject == nullptr) {
        ResultObject = NewObject<UObject>(this, RawClass);
    }
    if (ResultObject == nullptr) {
        return TGameStatusResult<UObject*>::Failure(5002, TEXT("Failed to instantiate UObject"));
    }

    {
        FObjectPoolBucket& Bucket = ObjectPoolMap.FindOrAdd(RawClass);
        Bucket.ActiveObjectSet.Add(ResultObject);
        ++Bucket.ActiveCount;
    }

    InvokePoolableCallback(ResultObject, true);
    FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass);
    if (!IsValid(ResultObject)) {
        if (Bucket != nullptr && Bucket->ActiveObjectSet.Remove(ResultObject) > 0) {
            --Bucket->ActiveCount;
        }
        return TGameStatusResult<UObject*>::Failure(5002, TEXT("UObject was invalidated during OnAcquireFromPool"));
    }
    if (Bucket == nullptr || !Bucket->ActiveObjectSet.Contains(ResultObject)) {
        return TGameStatusResult<UObject*>::Failure(5002, TEXT("UObject was invalidated during OnAcquireFromPool"));
    }

    return TGameStatusResult<UObject*>::Success(ResultObject);
}

/**
 * @brief 将已借出的非 Actor UObject 归还回池缓存（蓝图轻量门面）
 * @param Object 待归还的 UObject 实例
 */
void UObjectPoolSubsystem::K2_ReleaseObject(UObject* Object) {
    ReleaseObject(Object);
}

/**
 * @brief 将已借出的非 Actor UObject 归还回池缓存（强类型契约）
 * @param Object 待归还的 UObject 实例
 * @return 成功归还返回 Success，未借出或已销毁返回 Failure(5003/5004)
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseObject(UObject* Object) {
    if (!IsValid(Object)) {
        return TGameStatusResult<void>::Failure(5004, TEXT("Object is invalid or already destroyed"));
    }

    UClass* RawClass = Object->GetClass();
    FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass);
    if (Bucket == nullptr || Bucket->ActiveObjectSet.Remove(Object) == 0) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameObjectPool, Warning, TEXT("【对象池】归还警告：UObject [%s] 不是当前活跃池属实例，已忽略归还。"),
                *GetNameSafe(Object));
        }
        return TGameStatusResult<void>::Failure(5003, TEXT("Object is not an active member of this pool"));
    }
    --Bucket->ActiveCount;

    InvokePoolableCallback(Object, false);
    if (!IsValid(Object)) {
        return TGameStatusResult<void>::Success();
    }

    FObjectPoolBucket& CurrentBucket = ObjectPoolMap.FindOrAdd(RawClass);
    if (CurrentBucket.MaxIdleCount > 0 && CurrentBucket.IdleObjects.Num() >= CurrentBucket.MaxIdleCount) {
        Object->MarkAsGarbage();
    } else {
        CurrentBucket.IdleObjects.Add(Object);
        CurrentBucket.IdleObjectSet.Add(Object);
    }
    return TGameStatusResult<void>::Success();
}

FPoolStats UObjectPoolSubsystem::GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const {
    FPoolStats Stats;
    const UClass* RawClass = ObjectClass.Get();
    if (RawClass == nullptr) {
        return Stats;
    }

    if (const FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass)) {
        Stats.IdleCount = Bucket->IdleObjects.Num();
        Stats.ActiveCount = Bucket->ActiveCount;
        Stats.TotalCount = Stats.IdleCount + Stats.ActiveCount;
    }
    return Stats;
}

void UObjectPoolSubsystem::ClearObjectPool(TSubclassOf<UObject> ObjectClass) {
    UClass* RawClass = ObjectClass.Get();
    if (RawClass == nullptr) {
        return;
    }
    if (FObjectPoolBucket* Bucket = ObjectPoolMap.Find(RawClass)) {
        DestroyBucketIdleObjects(*Bucket);
    }
}

void UObjectPoolSubsystem::ClearAllObjectPools() {
    TArray<TObjectPtr<UClass>> ObjectClasses;
    ObjectPoolMap.GenerateKeyArray(ObjectClasses);
    for (const TObjectPtr<UClass>& ObjectClass : ObjectClasses) {
        if (FObjectPoolBucket* Bucket = ObjectPoolMap.Find(ObjectClass)) {
            DestroyBucketIdleObjects(*Bucket);
        }
    }
}

void UObjectPoolSubsystem::OnPooledActorDestroyed(AActor* DestroyedActor) {
    if (!DestroyedActor) {
        return;
    }

    TSubclassOf<AActor> ActorClass = DestroyedActor->GetClass();
    if (FPoolBucket* Bucket = PoolMap.Find(ActorClass)) {
        Bucket->IdleActorSet.Remove(DestroyedActor);
        Bucket->IdleActors.Remove(DestroyedActor);
        if (Bucket->ActiveActorSet.Remove(DestroyedActor) > 0) {
            --Bucket->ActiveCount;
        }
    }
}

bool UObjectPoolSubsystem::SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass) {
    UWorld* World = GetWorld();
    if (World == nullptr || !ActorClass) {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* NewActor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
    if (NewActor == nullptr) {
        return false;
    }

    DeactivateActorForPool(NewActor);
    FPoolBucket& Bucket = PoolMap.FindOrAdd(ActorClass);
    Bucket.IdleActors.Add(NewActor);
    Bucket.IdleActorSet.Add(NewActor);
    return true;
}

void UObjectPoolSubsystem::DestroyBucketIdleActors(FPoolBucket& Bucket) {
    TArray<TObjectPtr<AActor>> IdleActors = MoveTemp(Bucket.IdleActors);
    Bucket.IdleActors.Empty();
    Bucket.IdleActorSet.Empty();
    for (TObjectPtr<AActor>& IdleActor : IdleActors) {
        if (IsValid(IdleActor.Get())) {
            IdleActor->OnDestroyed.RemoveAll(this);
            IdleActor->Destroy();
        }
    }
}

void UObjectPoolSubsystem::DestroyBucketIdleObjects(FObjectPoolBucket& Bucket) {
    TArray<TObjectPtr<UObject>> IdleObjects = MoveTemp(Bucket.IdleObjects);
    Bucket.IdleObjects.Empty();
    Bucket.IdleObjectSet.Empty();
    for (TObjectPtr<UObject>& IdleObj : IdleObjects) {
        if (IsValid(IdleObj.Get())) {
            IdleObj->MarkAsGarbage();
        }
    }
}
