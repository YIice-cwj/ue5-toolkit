#include "Subsystem/UGameSpatialSubsystem.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"

const FGameplayTag& UGameSpatialSubsystem::GetDefaultLayerTag() {
    static const FGameplayTag DefaultTag = UGameplayTagsManager::Get().AddNativeGameplayTag(
        FName("Spatial.Layer.Default"),
        TEXT("默认空间图层")
    );
    return DefaultTag;
}

UGameSpatialSubsystem::UGameSpatialSubsystem() {
}

void UGameSpatialSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    ClearAll();
}

void UGameSpatialSubsystem::Deinitialize() {
    ClearAll();
    Super::Deinitialize();
}

bool UGameSpatialSubsystem::ShouldCreateSubsystem(UObject* Outer) const {
    return true;
}

bool UGameSpatialSubsystem::RegisterActor(AActor* Actor, FGameplayTag LayerTag) {
    if (Actor == nullptr || Actor->IsActorBeingDestroyed()) {
        return false;
    }

    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TWeakObjectPtr<AActor> WeakActor(Actor);

    if (FActorSpatialEntry* ExistingEntry = TrackedActors.Find(WeakActor)) {
        if (ExistingEntry->LayerTag == ResolvedTag) {
            return UpdateActor(Actor);
        }
        UnregisterActor(Actor);
    }

    const FVector Location = Actor->GetActorLocation();
    LayeredGrids.FindOrAdd(ResolvedTag).Insert(WeakActor, Location);
    TrackedActors.Add(WeakActor, FActorSpatialEntry{ResolvedTag, Location});
    return true;
}

int32 UGameSpatialSubsystem::RegisterActors(const TArray<AActor*>& Actors, FGameplayTag LayerTag) {
    int32 RegisteredCount = 0;
    for (AActor* Actor : Actors) {
        if (RegisterActor(Actor, LayerTag)) {
            ++RegisteredCount;
        }
    }
    return RegisteredCount;
}

bool UGameSpatialSubsystem::UnregisterActor(AActor* Actor) {
    if (Actor == nullptr) {
        return false;
    }

    const TWeakObjectPtr<AActor> WeakActor(Actor);
    FActorSpatialEntry Entry;
    if (!TrackedActors.RemoveAndCopyValue(WeakActor, Entry)) {
        return false;
    }

    if (TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(Entry.LayerTag)) {
        Grid->Remove(WeakActor);
    }
    return true;
}

int32 UGameSpatialSubsystem::UnregisterActors(const TArray<AActor*>& Actors) {
    int32 UnregisteredCount = 0;
    for (AActor* Actor : Actors) {
        if (UnregisterActor(Actor)) {
            ++UnregisteredCount;
        }
    }
    return UnregisteredCount;
}

bool UGameSpatialSubsystem::UpdateActorLocation(AActor* Actor, const FVector& NewLocation) {
    if (Actor == nullptr || Actor->IsActorBeingDestroyed()) {
        return false;
    }

    const TWeakObjectPtr<AActor> WeakActor(Actor);
    FActorSpatialEntry* Entry = TrackedActors.Find(WeakActor);
    if (Entry == nullptr) {
        return false;
    }

    TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(Entry->LayerTag);
    if (Grid == nullptr) {
        return false;
    }

    Grid->Update(WeakActor, NewLocation);
    Entry->LastLocation = NewLocation;
    return true;
}

bool UGameSpatialSubsystem::UpdateActor(AActor* Actor) {
    if (Actor == nullptr || Actor->IsActorBeingDestroyed()) {
        return false;
    }
    return UpdateActorLocation(Actor, Actor->GetActorLocation());
}

void UGameSpatialSubsystem::QueryActorsInRadius(
    const FVector& Center,
    float Radius,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) const {
    OutActors.Reset();
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return;
    }

    Grid->QueryRadiusMatching(Center, Radius, [&OutActors](const TWeakObjectPtr<AActor>& WeakActor, const FVector&) {
        if (AActor* Actor = WeakActor.Get()) {
            OutActors.Add(Actor);
        }
    });
}

void UGameSpatialSubsystem::QueryActorsInCone(
    const FVector& Apex,
    const FVector& Direction,
    float Radius,
    float HalfAngleDegrees,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) const {
    OutActors.Reset();
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return;
    }

    Grid->QueryConeMatching(Apex, Direction, Radius, HalfAngleDegrees, [&OutActors](const TWeakObjectPtr<AActor>& WeakActor, const FVector&) {
        if (AActor* Actor = WeakActor.Get()) {
            OutActors.Add(Actor);
        }
    });
}

void UGameSpatialSubsystem::QueryActorsInCylinder(
    const FVector& Center,
    float Radius,
    float HalfHeight,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) const {
    OutActors.Reset();
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return;
    }

    Grid->QueryCylinderMatching(Center, Radius, HalfHeight, [&OutActors](const TWeakObjectPtr<AActor>& WeakActor, const FVector&) {
        if (AActor* Actor = WeakActor.Get()) {
            OutActors.Add(Actor);
        }
    });
}

void UGameSpatialSubsystem::QueryActorsInAABB(
    const FBox& Box,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) const {
    OutActors.Reset();
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return;
    }

    Grid->QueryAABBMatching(Box, [&OutActors](const TWeakObjectPtr<AActor>& WeakActor, const FVector&) {
        if (AActor* Actor = WeakActor.Get()) {
            OutActors.Add(Actor);
        }
    });
}

bool UGameSpatialSubsystem::QueryNearestActor(
    const FVector& Center,
    float MaxSearchRadius,
    FGameplayTag LayerTag,
    AActor*& OutNearestActor
) const {
    OutNearestActor = nullptr;
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return false;
    }

    TWeakObjectPtr<AActor> NearestWeak;
    const bool bFound = Grid->QueryNearest(Center, MaxSearchRadius, NearestWeak, [](const TWeakObjectPtr<AActor>& Item) {
        return Item.IsValid();
    });

    if (!bFound) {
        return false;
    }

    OutNearestActor = NearestWeak.Get();
    return OutNearestActor != nullptr;
}

void UGameSpatialSubsystem::QueryKNearestActors(
    const FVector& Center,
    float MaxSearchRadius,
    int32 K,
    FGameplayTag LayerTag,
    TArray<AActor*>& OutActors
) const {
    OutActors.Reset();
    if (K <= 0) {
        return;
    }

    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return;
    }

    TArray<TWeakObjectPtr<AActor>> NearestList;
    Grid->QueryKNearest(Center, MaxSearchRadius, K, NearestList, [](const TWeakObjectPtr<AActor>& Item) {
        return Item.IsValid();
    });

    OutActors.Reserve(NearestList.Num());
    for (const TWeakObjectPtr<AActor>& WeakActor : NearestList) {
        if (AActor* Actor = WeakActor.Get()) {
            OutActors.Add(Actor);
        }
    }
}

void UGameSpatialSubsystem::ClearLayer(FGameplayTag LayerTag) {
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    LayeredGrids.Remove(ResolvedTag);

    for (auto It = TrackedActors.CreateIterator(); It; ++It) {
        if (It.Value().LayerTag == ResolvedTag) {
            It.RemoveCurrent();
        }
    }
}

void UGameSpatialSubsystem::ClearAll() {
    LayeredGrids.Empty();
    TrackedActors.Empty();
}

int32 UGameSpatialSubsystem::PruneStaleActors(FGameplayTag LayerTag) {
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return 0;
    }

    int32 PrunedCount = 0;
    for (auto It = TrackedActors.CreateIterator(); It; ++It) {
        if (It.Value().LayerTag == ResolvedTag && !It.Key().IsValid()) {
            Grid->Remove(It.Key());
            It.RemoveCurrent();
            ++PrunedCount;
        }
    }
    return PrunedCount;
}

int32 UGameSpatialSubsystem::PruneAllStaleActors() {
    int32 PrunedCount = 0;
    for (auto It = TrackedActors.CreateIterator(); It; ++It) {
        if (!It.Key().IsValid()) {
            if (TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(It.Value().LayerTag)) {
                Grid->Remove(It.Key());
            }
            It.RemoveCurrent();
            ++PrunedCount;
        }
    }
    return PrunedCount;
}

int32 UGameSpatialSubsystem::GetActorCount(FGameplayTag LayerTag) const {
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    return Grid != nullptr ? Grid->Num() : 0;
}

int32 UGameSpatialSubsystem::GetTotalActorCount() const {
    return TrackedActors.Num();
}

bool UGameSpatialSubsystem::IsActorRegistered(const AActor* Actor) const {
    if (Actor == nullptr) {
        return false;
    }
    return TrackedActors.Contains(Actor);
}

FGameplayTag UGameSpatialSubsystem::GetActorLayer(const AActor* Actor) const {
    if (Actor == nullptr) {
        return FGameplayTag();
    }
    const FActorSpatialEntry* Entry = TrackedActors.Find(Actor);
    return Entry != nullptr ? Entry->LayerTag : FGameplayTag();
}

bool UGameSpatialSubsystem::GetActorLocation(const AActor* Actor, FVector& OutLocation) const {
    OutLocation = FVector::ZeroVector;
    if (Actor == nullptr) {
        return false;
    }
    const FActorSpatialEntry* Entry = TrackedActors.Find(Actor);
    if (Entry == nullptr) {
        return false;
    }
    OutLocation = Entry->LastLocation;
    return true;
}

void UGameSpatialSubsystem::GetActorsInLayer(FGameplayTag LayerTag, TArray<AActor*>& OutActors) const {
    OutActors.Reset();
    const FGameplayTag ResolvedTag = ResolveLayerTag(LayerTag);
    const TSpatialHashGrid3D<TWeakObjectPtr<AActor>>* Grid = LayeredGrids.Find(ResolvedTag);
    if (Grid == nullptr) {
        return;
    }

    TArray<TWeakObjectPtr<AActor>> ElementList;
    Grid->GetAllElements(ElementList);
    OutActors.Reserve(ElementList.Num());
    for (const TWeakObjectPtr<AActor>& WeakActor : ElementList) {
        if (AActor* Actor = WeakActor.Get()) {
            OutActors.Add(Actor);
        }
    }
}

FGameplayTag UGameSpatialSubsystem::ResolveLayerTag(const FGameplayTag& InTag) const {
    if (InTag.IsValid()) {
        return InTag;
    }
    return GetDefaultLayerTag();
}
