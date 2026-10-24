#include "NetSmoke/NetSmokeHarness.h"

#include "Combat/UGameBuffComponent.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace {
constexpr float PollIntervalSeconds = 0.25f;
constexpr double TimeoutSeconds = 90.0;

const FGameplayTag& GetSmokeBuffTag() {
    static const FGameplayTag Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("NetSmoke.Buff")));
    return Tag;
}

const FGameplayTag& GetSmokeStateTag() {
    static const FGameplayTag Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.NetSmoke.Active")));
    return Tag;
}
}

void NetSmokeResult::Write(const TCHAR* InFileName, const FString& InContent) {
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("NetSmoke");
    IFileManager::Get().MakeDirectory(*Directory, true);
    FFileHelper::SaveStringToFile(InContent, *(Directory / InFileName));
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

ANetSmokeGameMode::ANetSmokeGameMode() {
    DefaultPawnClass = ANetSmokePawn::StaticClass();
    PlayerControllerClass = ANetSmokePlayerController::StaticClass();
}
