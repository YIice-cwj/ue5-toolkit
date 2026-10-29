#include "NetSmoke/ANetSmokePlayerController.h"

#include "Buff/UGameBuffComponent.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "NetSmoke/NetSmokeResult.h"
#include "TimerManager.h"

namespace {
constexpr float PollIntervalSeconds = 0.25f;
constexpr double TimeoutSeconds = 90.0;

const FGameplayTag& GetSmokeStateTag() {
    static const FGameplayTag Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.NetSmoke.Active")));
    return Tag;
}
}

void ANetSmokePlayerController::BeginPlay() {
    Super::BeginPlay();

    if (GetNetMode() != NM_Client) {
        return;
    }

    StartSeconds = FPlatformTime::Seconds();
    GetWorldTimerManager().SetTimer(PollTimer, this, &ANetSmokePlayerController::PollReplicatedBuff, PollIntervalSeconds, true);
}

void ANetSmokePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    GetWorldTimerManager().ClearTimer(PollTimer);
    Super::EndPlay(EndPlayReason);
}

void ANetSmokePlayerController::PollReplicatedBuff() {
    FGameplayTagContainer GrantedTags;
    if (const APawn* MyPawn = GetPawn()) {
        if (const UGameBuffComponent* Buffs = MyPawn->FindComponentByClass<UGameBuffComponent>()) {
            Buffs->GetGrantedTags(GrantedTags);
        }
    }

    if (GrantedTags.HasTag(GetSmokeStateTag())) {
        Finish(TEXT("PASS"));
        return;
    }

    if (FPlatformTime::Seconds() - StartSeconds > TimeoutSeconds) {
        Finish(TEXT("TIMEOUT"));
    }
}

void ANetSmokePlayerController::Finish(const FString& InResult) {
    GetWorldTimerManager().ClearTimer(PollTimer);
    NetSmokeResult::Write(TEXT("ClientResult.txt"), InResult);
    UE_LOG(LogTemp, Display, TEXT("【NetSmoke】客户端结果: %s"), *InResult);
    FGenericPlatformMisc::RequestExit(false);
}

