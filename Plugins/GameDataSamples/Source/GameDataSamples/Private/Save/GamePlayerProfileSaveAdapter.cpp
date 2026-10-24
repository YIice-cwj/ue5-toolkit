/**
 * @file GamePlayerProfileSaveAdapter.cpp
 * @brief 玩家档案存档适配器实现
 */

#include "Save/GamePlayerProfileSaveAdapter.h"

#include "Dynamic/GamePlayerDataRegistry.h"
#include "Persistence/USaveGameSubsystem.h"
#include "Serialization/Archive.h"

const FName UGamePlayerProfileSaveAdapter::SegmentName(TEXT("Player.Profile"));

namespace {

void SerializeProfile(FArchive& Ar, FPlayerProfileData& Profile) {
    Ar << Profile.PlayerId
       << Profile.Progression.Level
       << Profile.Progression.Experience
       << Profile.Progression.Gold
       << Profile.ArchiveVersion;
}

}  // namespace

void UGamePlayerProfileSaveAdapter::BeginDestroy() {
    Teardown();
    Super::BeginDestroy();
}

void UGamePlayerProfileSaveAdapter::Setup(USaveGameSubsystem* InSaveSubsystem, UGamePlayerDataRegistry* InRegistry) {
    Teardown();
    if (InSaveSubsystem == nullptr || InRegistry == nullptr) {
        return;
    }

    SaveSubsystem = InSaveSubsystem;
    DataRegistry = InRegistry;
    TWeakObjectPtr<UGamePlayerDataRegistry> WeakRegistry(InRegistry);

    FGameSaveSegment Segment;
    Segment.SegmentName = SegmentName;
    Segment.SegmentVersion = 1;
    Segment.Save = [WeakRegistry](FArchive& Ar) {
        if (const UGamePlayerDataRegistry* Target = WeakRegistry.Get()) {
            FPlayerProfileData Profile = Target->GetProfile();
            SerializeProfile(Ar, Profile);
        } else {
            Ar.SetError();
        }
    };
    Segment.Load = [WeakRegistry](FArchive& Ar, [[maybe_unused]] int32 StoredVersion) {
        FPlayerProfileData Profile;
        SerializeProfile(Ar, Profile);
        if (Ar.IsError()) {
            return false;
        }

        UGamePlayerDataRegistry* Target = WeakRegistry.Get();
        if (Target == nullptr) {
            return false;
        }
        Target->SetProfile(Profile);
        return true;
    };

    SaveSubsystem->RegisterSegment(Segment);
}

void UGamePlayerProfileSaveAdapter::Teardown() {
    if (SaveSubsystem != nullptr) {
        SaveSubsystem->UnregisterSegment(SegmentName);
    }
    SaveSubsystem = nullptr;
    DataRegistry = nullptr;
}
