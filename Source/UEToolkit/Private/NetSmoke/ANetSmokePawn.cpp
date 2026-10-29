#include "NetSmoke/ANetSmokePawn.h"

#include "Buff/UGameBuffComponent.h"
#include "GameplayTagsManager.h"
#include "NetSmoke/NetSmokeResult.h"

namespace {
const FGameplayTag& GetSmokeBuffTag() {
    static const FGameplayTag Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("NetSmoke.Buff")));
    return Tag;
}

const FGameplayTag& GetSmokeStateTag() {
    static const FGameplayTag Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.NetSmoke.Active")));
    return Tag;
}
}

ANetSmokePawn::ANetSmokePawn() {
    BuffComponent = CreateDefaultSubobject<UGameBuffComponent>(TEXT("GameBuffComponent"));
}

void ANetSmokePawn::BeginPlay() {
    Super::BeginPlay();

    if (!HasAuthority()) {
        return;
    }

    FGameBuffSpec Spec;
    Spec.BuffTag = GetSmokeBuffTag();
    Spec.GrantedTags.AddTag(GetSmokeStateTag());

    const int32 InstanceId = BuffComponent->ApplyBuff(Spec);
    NetSmokeResult::Write(TEXT("ServerResult.txt"), InstanceId != 0 ? TEXT("APPLIED") : TEXT("APPLY_FAILED"));
}

