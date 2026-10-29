/**
 * @file GamePlayerProfileSaveAdapter.cpp
 * @brief 玩家档案存档适配器实现
 */

#include "Save/GamePlayerProfileSaveAdapter.h"

#include "Dynamic/GamePlayerDataRegistry.h"
#include "Save/USaveGameSubsystem.h"
#include "Serialization/Archive.h"
#include "Logging/GameLogCategories.h"

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
        GAME_LOG(Warning, TEXT("【玩家档案存档适配器】Setup 失败：SaveSubsystem 或 InRegistry 为空。"));
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
    GAME_LOG(Log, TEXT("【玩家档案存档适配器】成功建立绑定并注册存档段 [%s]。"), *SegmentName.ToString());
}

void UGamePlayerProfileSaveAdapter::Teardown() {
    if (SaveSubsystem != nullptr) {
        SaveSubsystem->UnregisterSegment(SegmentName);
        GAME_LOG(Log, TEXT("【玩家档案存档适配器】已注销存档段 [%s]。"), *SegmentName.ToString());
    }
    SaveSubsystem = nullptr;
    DataRegistry = nullptr;
}
