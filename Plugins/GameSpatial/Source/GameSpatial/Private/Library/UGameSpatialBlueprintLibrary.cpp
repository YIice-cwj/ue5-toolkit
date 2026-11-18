#include "Library/UGameSpatialBlueprintLibrary.h"
#include "Subsystem/UGameSpatialSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UGameSpatialSubsystem* UGameSpatialBlueprintLibrary::GetGameSpatialSubsystem(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr) {
        return nullptr;
    }
    const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (World == nullptr) {
        return nullptr;
    }
    return World->GetSubsystem<UGameSpatialSubsystem>();
}

bool UGameSpatialBlueprintLibrary::RegisterSpatialActor(const UObject* WorldContextObject, AActor* Actor, FGameplayTag LayerTag) {
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr && Subsystem->RegisterActor(Actor, LayerTag);
}

int32 UGameSpatialBlueprintLibrary::RegisterSpatialActors(const UObject* WorldContextObject, const TArray<AActor*>& Actors, FGameplayTag LayerTag) {
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr ? Subsystem->RegisterActors(Actors, LayerTag) : 0;
}

bool UGameSpatialBlueprintLibrary::UnregisterSpatialActor(const UObject* WorldContextObject, AActor* Actor) {
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr && Subsystem->UnregisterActor(Actor);
}

int32 UGameSpatialBlueprintLibrary::UnregisterSpatialActors(const UObject* WorldContextObject, const TArray<AActor*>& Actors) {
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr ? Subsystem->UnregisterActors(Actors) : 0;
}

bool UGameSpatialBlueprintLibrary::UpdateSpatialActorLocation(const UObject* WorldContextObject, AActor* Actor, const FVector& NewLocation) {
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr && Subsystem->UpdateActorLocation(Actor, NewLocation);
}

bool UGameSpatialBlueprintLibrary::UpdateSpatialActor(const UObject* WorldContextObject, AActor* Actor) {
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr && Subsystem->UpdateActor(Actor);
}

void UGameSpatialBlueprintLibrary::QuerySpatialActorsInRadius(
    const UObject* WorldContextObject,
    const FVector& Center,
    float Radius,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) {
    OutActors.Reset();
    if (UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject)) {
        Subsystem->QueryActorsInRadius(Center, Radius, LayerTag, OutActors);
    }
}

void UGameSpatialBlueprintLibrary::QuerySpatialActorsInCone(
    const UObject* WorldContextObject,
    const FVector& Apex,
    const FVector& Direction,
    float Radius,
    float HalfAngleDegrees,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) {
    OutActors.Reset();
    if (UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject)) {
        Subsystem->QueryActorsInCone(Apex, Direction, Radius, HalfAngleDegrees, LayerTag, OutActors);
    }
}

void UGameSpatialBlueprintLibrary::QuerySpatialActorsInCylinder(
    const UObject* WorldContextObject,
    const FVector& Center,
    float Radius,
    float HalfHeight,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) {
    OutActors.Reset();
    if (UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject)) {
        Subsystem->QueryActorsInCylinder(Center, Radius, HalfHeight, LayerTag, OutActors);
    }
}

void UGameSpatialBlueprintLibrary::QuerySpatialActorsInAABB(
    const UObject* WorldContextObject,
    const FBox& Box,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) {
    OutActors.Reset();
    if (UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject)) {
        Subsystem->QueryActorsInAABB(Box, LayerTag, OutActors);
    }
}

bool UGameSpatialBlueprintLibrary::QueryNearestSpatialActor(
    const UObject* WorldContextObject,
    const FVector& Center,
    float MaxSearchRadius,
    FGameplayTag LayerTag,
    AActor*& OutNearestActor
) {
    OutNearestActor = nullptr;
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr && Subsystem->QueryNearestActor(Center, MaxSearchRadius, LayerTag, OutNearestActor);
}

void UGameSpatialBlueprintLibrary::QueryKNearestSpatialActors(
    const UObject* WorldContextObject,
    const FVector& Center,
    float MaxSearchRadius,
    int32 K,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) {
    OutActors.Reset();
    if (UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject)) {
        Subsystem->QueryKNearestActors(Center, MaxSearchRadius, K, LayerTag, OutActors);
    }
}

int32 UGameSpatialBlueprintLibrary::PruneAllSpatialStaleActors(const UObject* WorldContextObject) {
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr ? Subsystem->PruneAllStaleActors() : 0;
}

void UGameSpatialBlueprintLibrary::ClearSpatialLayer(const UObject* WorldContextObject, FGameplayTag LayerTag) {
    if (UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject)) {
        Subsystem->ClearLayer(LayerTag);
    }
}

void UGameSpatialBlueprintLibrary::ClearAllSpatialLayers(const UObject* WorldContextObject) {
    if (UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject)) {
        Subsystem->ClearAll();
    }
}

int32 UGameSpatialBlueprintLibrary::PruneSpatialStaleActors(const UObject* WorldContextObject, FGameplayTag LayerTag) {
    UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr ? Subsystem->PruneStaleActors(LayerTag) : 0;
}

int32 UGameSpatialBlueprintLibrary::GetSpatialActorCount(const UObject* WorldContextObject, FGameplayTag LayerTag) {
    const UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr ? Subsystem->GetActorCount(LayerTag) : 0;
}

int32 UGameSpatialBlueprintLibrary::GetTotalSpatialActorCount(const UObject* WorldContextObject) {
    const UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr ? Subsystem->GetTotalActorCount() : 0;
}

bool UGameSpatialBlueprintLibrary::IsSpatialActorRegistered(const UObject* WorldContextObject, const AActor* Actor) {
    const UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr && Subsystem->IsActorRegistered(Actor);
}

FGameplayTag UGameSpatialBlueprintLibrary::GetSpatialActorLayer(const UObject* WorldContextObject, const AActor* Actor) {
    const UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr ? Subsystem->GetActorLayer(Actor) : FGameplayTag();
}

bool UGameSpatialBlueprintLibrary::GetSpatialActorLocation(const UObject* WorldContextObject, const AActor* Actor, FVector& OutLocation) {
    OutLocation = FVector::ZeroVector;
    const UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject);
    return Subsystem != nullptr && Subsystem->GetActorLocation(Actor, OutLocation);
}

void UGameSpatialBlueprintLibrary::GetSpatialActorsInLayer(const UObject* WorldContextObject, FGameplayTag LayerTag, TArray<AActor*>& OutActors) {
    OutActors.Reset();
    if (const UGameSpatialSubsystem* Subsystem = GetGameSpatialSubsystem(WorldContextObject)) {
        Subsystem->GetActorsInLayer(LayerTag, OutActors);
    }
}
