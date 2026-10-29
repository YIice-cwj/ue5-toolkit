/**
 * @file USaveGameSubsystem.cpp
 * @brief 通用存档与持久化调度子系统实现
 */

#include "Save/USaveGameSubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "Logging/GameLogCategories.h"
#include "Tasks/FGameThreadTaskRunner.h"

USaveGameSubsystem::USaveGameSubsystem()
    : SaveGameClass(UGameSaveArchive::StaticClass()) {
}

FString USaveGameSubsystem::GetSaveSlotFilePath(const FString& SlotName) {
    return FSaveGameFileIO::GetSaveSlotFilePath(SlotName);
}

FString USaveGameSubsystem::GetSaveSlotTempFilePath(const FString& SlotName) {
    return FSaveGameFileIO::GetSaveSlotTempFilePath(SlotName);
}

void USaveGameSubsystem::Deinitialize() {
    SegmentRegistry.Reset();
    AllowedSlotNames.Empty();
    Super::Deinitialize();
}

void USaveGameSubsystem::SetSaveGameClass(TSubclassOf<UGameSaveArchive> InSaveGameClass) {
    if (InSaveGameClass != nullptr) {
        SaveGameClass = InSaveGameClass;
    }
}

TSubclassOf<UGameSaveArchive> USaveGameSubsystem::GetSaveGameClass() const {
    return SaveGameClass;
}

void USaveGameSubsystem::SetAllowedSlotNames(const TArray<FString>& InAllowedSlotNames) {
    AllowedSlotNames.Reset();
    AllowedSlotNames.Append(InAllowedSlotNames);
}

bool USaveGameSubsystem::IsSlotNameAllowed(const FString& SlotName) const {
    return FSaveGameFileIO::IsSlotNameAllowed(SlotName, AllowedSlotNames);
}

bool USaveGameSubsystem::SaveGameToSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        return false;
    }

    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>(this, SaveGameClass);
    if (!SegmentRegistry.BuildArchive(SlotName, *Archive)) {
        return false;
    }

    TArray<uint8> Bytes;
    if (!FSaveGameFileIO::SerializeObject(Archive, Bytes)) {
        GAME_LOG(Error, TEXT("【存档子系统】同步保存序列化失败: %s"), *SlotName);
        return false;
    }

    const uint64 SaveGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    const bool bSuccess = FSaveGameFileIO::WriteSlotBytes(SlotName, Bytes, SaveGeneration);
    if (bSuccess) {
        GAME_LOG(Log, TEXT("【存档子系统】同步保存槽位成功: %s"), *SlotName);
    } else {
        GAME_LOG(Error, TEXT("【存档子系统】同步保存槽位失败: %s"), *SlotName);
    }
    return bSuccess;
}

bool USaveGameSubsystem::SaveGameToSlotAsync(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>(this, SaveGameClass);
    if (!SegmentRegistry.BuildArchive(SlotName, *Archive)) {
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    TArray<uint8> Bytes;
    if (!FSaveGameFileIO::SerializeObject(Archive, Bytes)) {
        GAME_LOG(Error, TEXT("【存档子系统】内存序列化失败: %s"), *SlotName);
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    const uint64 SaveGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    TWeakObjectPtr<USaveGameSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(WeakThis, [WeakThis, SlotName, SaveGeneration, Payload = MoveTemp(Bytes)]() mutable {
        const bool bIoSuccess = FSaveGameFileIO::WriteSlotBytes(SlotName, Payload, SaveGeneration);
        FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, SlotName, bIoSuccess]() {
            if (bIoSuccess) {
                GAME_LOG(Log, TEXT("【存档子系统】异步保存槽位成功: %s"), *SlotName);
            } else {
                GAME_LOG(Error, TEXT("【存档子系统】异步保存槽位失败: %s"), *SlotName);
            }
            WeakThis->OnSaveComplete.Broadcast(SlotName, bIoSuccess);
        });
    });

    return true;
}

bool USaveGameSubsystem::LoadGameFromSlot(const FString& SlotName) {
    USaveGame* LoadedObject = nullptr;
    if (!LoadObjectFromSlot(SlotName, SaveGameClass, LoadedObject)) {
        GAME_LOG(Warning, TEXT("【存档子系统】同步装载槽位失败: %s"), *SlotName);
        return false;
    }

    UGameSaveArchive* Archive = CastChecked<UGameSaveArchive>(LoadedObject);
    if (Archive->Metadata.ArchiveVersion > UGameSaveArchive::CurrentArchiveVersion) {
        GAME_LOG(Error, TEXT("【存档子系统】存档版本 %d 高于当前支持版本 %d，拒绝加载: %s"),
            Archive->Metadata.ArchiveVersion, UGameSaveArchive::CurrentArchiveVersion, *SlotName);
        return false;
    }

    if (!SegmentRegistry.ApplyArchive(SlotName, *Archive)) {
        GAME_LOG(Warning, TEXT("【存档子系统】同步装载槽位段应用失败: %s"), *SlotName);
        return false;
    }

    GAME_LOG(Log, TEXT("【存档子系统】同步成功装载槽位: %s"), *SlotName);
    return true;
}

bool USaveGameSubsystem::LoadGameFromSlotAsync(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        OnLoadComplete.Broadcast(SlotName, false);
        return false;
    }

    TWeakObjectPtr<USaveGameSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(WeakThis, [WeakThis, SlotName]() {
        TArray<uint8> Bytes;
        const bool bReadSuccess = FSaveGameFileIO::ReadSlotBytes(SlotName, Bytes);

        FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, SlotName, bReadSuccess, Payload = MoveTemp(Bytes)]() mutable {
            bool bSuccess = false;
            if (bReadSuccess) {
                USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(Payload);
                if (LoadedObject != nullptr && LoadedObject->IsA(WeakThis->SaveGameClass)) {
                    UGameSaveArchive* Archive = CastChecked<UGameSaveArchive>(LoadedObject);
                    bSuccess = Archive->Metadata.ArchiveVersion <= UGameSaveArchive::CurrentArchiveVersion
                        && WeakThis->SegmentRegistry.ApplyArchive(SlotName, *Archive);
                }
            }

            if (bSuccess) {
                GAME_LOG(Log, TEXT("【存档子系统】异步成功装载槽位: %s"), *SlotName);
            } else {
                GAME_LOG(Error, TEXT("【存档子系统】异步装载槽位失败: %s"), *SlotName);
            }
            WeakThis->OnLoadComplete.Broadcast(SlotName, bSuccess);
        });
    });

    return true;
}

bool USaveGameSubsystem::SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject) {
    if (SaveGameObject == nullptr || !IsSlotNameAllowed(SlotName)) {
        return false;
    }

    TArray<uint8> Bytes;
    if (!FSaveGameFileIO::SerializeObject(SaveGameObject, Bytes)) {
        GAME_LOG(Error, TEXT("【存档子系统】自定义对象序列化失败: %s"), *SlotName);
        return false;
    }

    const uint64 SaveGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    return FSaveGameFileIO::WriteSlotBytes(SlotName, Bytes, SaveGeneration);
}

bool USaveGameSubsystem::LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad, USaveGame*& OutSaveGame) {
    OutSaveGame = nullptr;
    if (SaveGameClassToLoad == nullptr || !IsSlotNameAllowed(SlotName)) {
        return false;
    }

    TArray<uint8> Bytes;
    if (!FSaveGameFileIO::ReadSlotBytes(SlotName, Bytes)) {
        return false;
    }

    USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(Bytes);
    if (LoadedObject == nullptr || !LoadedObject->IsA(SaveGameClassToLoad)) {
        GAME_LOG(Error, TEXT("【存档子系统】存档反序列化失败或类型不匹配: %s"), *SlotName);
        return false;
    }

    OutSaveGame = LoadedObject;
    return true;
}

bool USaveGameSubsystem::DoesSaveSlotExist(const FString& SlotName) const {
    if (!IsSlotNameAllowed(SlotName)) {
        return false;
    }
    return FSaveGameFileIO::DoesSaveSlotExist(SlotName);
}

bool USaveGameSubsystem::DeleteSaveSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        return false;
    }
    SegmentRegistry.ClearPreservedSegmentsForSlot(SlotName);
    return FSaveGameFileIO::DeleteSaveSlot(SlotName);
}

void USaveGameSubsystem::RegisterSegment(const FGameSaveSegment& Segment) {
    SegmentRegistry.RegisterSegment(Segment);
}

bool USaveGameSubsystem::UnregisterSegment(FName SegmentName) {
    return SegmentRegistry.UnregisterSegment(SegmentName);
}

bool USaveGameSubsystem::HasSegment(FName SegmentName) const {
    return SegmentRegistry.HasSegment(SegmentName);
}

