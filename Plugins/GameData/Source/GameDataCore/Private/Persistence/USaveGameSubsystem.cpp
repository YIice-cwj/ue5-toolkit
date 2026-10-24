/**
 * @file USaveGameSubsystem.cpp
 * @brief 通用存档与持久化调度子系统实现
 */

#include "Persistence/USaveGameSubsystem.h"

#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Logging/GameLogCategories.h"
#include "Tasks/FGameThreadTaskRunner.h"

namespace {

FCriticalSection& GetSlotIoMutex() {
    static FCriticalSection Mutex;
    return Mutex;
}

TMap<FString, uint64>& GetIssuedSlotGenerations() {
    static TMap<FString, uint64> Generations;
    return Generations;
}

TMap<FString, uint64>& GetCommittedSlotGenerations() {
    static TMap<FString, uint64> Generations;
    return Generations;
}

uint64 IssueSlotSaveGeneration(const FString& SlotName) {
    FScopeLock Lock(&GetSlotIoMutex());
    const uint64* Committed = GetCommittedSlotGenerations().Find(SlotName);
    if (Committed != nullptr && *Committed == MAX_uint64) {
        GetCommittedSlotGenerations().Remove(SlotName);
    }
    uint64& Generation = GetIssuedSlotGenerations().FindOrAdd(SlotName);
    return ++Generation;
}

void InvalidateSlotSaveGeneration(const FString& SlotName) {
    FScopeLock Lock(&GetSlotIoMutex());
    GetIssuedSlotGenerations().Remove(SlotName);
    GetCommittedSlotGenerations().Add(SlotName, MAX_uint64);
}

bool HasValidSaveGameHeader(const TArray<uint8>& Bytes, const FString& SlotName) {
    static constexpr int32 SaveGameFileTypeTag = 0x53415647;
    static constexpr int32 MinSaveGameHeaderBytes = 64;
    if (Bytes.Num() < MinSaveGameHeaderBytes) {
        GAME_LOG(Warning, TEXT("【存档子系统】存档头长度不足，判定为损坏档: %s"), *SlotName);
        return false;
    }
    int32 FileTypeTag = 0;
    FMemory::Memcpy(&FileTypeTag, Bytes.GetData(), sizeof(FileTypeTag));
    if (FileTypeTag != SaveGameFileTypeTag) {
        GAME_LOG(Warning, TEXT("【存档子系统】存档 magic 不匹配，判定为损坏档: %s"), *SlotName);
        return false;
    }
    return true;
}

}  // namespace

USaveGameSubsystem::USaveGameSubsystem()
    : SaveGameClass(UGameSaveArchive::StaticClass()) {
}

FString USaveGameSubsystem::GetSaveSlotFilePath(const FString& SlotName) {
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav"));
}

FString USaveGameSubsystem::GetSaveSlotTempFilePath(const FString& SlotName) {
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav.tmp"));
}

void USaveGameSubsystem::Deinitialize() {
    Segments.Empty();
    PreservedSegmentsBySlot.Empty();
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
    if (SlotName.IsEmpty() || SlotName.Len() > 64) {
        return false;
    }
    if (SlotName.Contains(TEXT("/")) || SlotName.Contains(TEXT("\\")) || SlotName.Contains(TEXT("."))) {
        return false;
    }
    return AllowedSlotNames.IsEmpty() || AllowedSlotNames.Contains(SlotName);
}

bool USaveGameSubsystem::SaveGameToSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        return false;
    }

    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>(this, SaveGameClass);
    if (!BuildArchive(SlotName, *Archive)) {
        return false;
    }

    TArray<uint8> Bytes;
    if (!SerializeObject(Archive, Bytes)) {
        GAME_LOG(Error, TEXT("【存档子系统】同步保存序列化失败: %s"), *SlotName);
        return false;
    }

    const uint64 SaveGeneration = IssueSlotSaveGeneration(SlotName);
    const bool bSuccess = WriteSlotBytes(SlotName, Bytes, SaveGeneration);
    if (bSuccess) {
        GAME_LOG(Log, TEXT("【存档子系统】同步保存槽位成功: %s"), *SlotName);
    } else {
        GAME_LOG(Error, TEXT("【存档子系统】同步保存槽位失败（写盘或原子替换失败）: %s"), *SlotName);
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
    if (!BuildArchive(SlotName, *Archive)) {
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    TArray<uint8> Bytes;
    if (!SerializeObject(Archive, Bytes)) {
        GAME_LOG(Error, TEXT("【存档子系统】内存序列化失败: %s"), *SlotName);
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    const uint64 SaveGeneration = IssueSlotSaveGeneration(SlotName);
    TWeakObjectPtr<USaveGameSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(WeakThis, [WeakThis, SlotName, SaveGeneration, Payload = MoveTemp(Bytes)]() mutable {
        const bool bIoSuccess = WriteSlotBytes(SlotName, Payload, SaveGeneration);
        FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, SlotName, bIoSuccess]() {
            if (bIoSuccess) {
                GAME_LOG(Log, TEXT("【存档子系统】异步保存槽位成功: %s"), *SlotName);
            } else {
                GAME_LOG(Error, TEXT("【存档子系统】异步保存槽位失败（写盘或原子替换失败）: %s"), *SlotName);
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

    if (!ApplyArchive(SlotName, *Archive)) {
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
        const bool bReadSuccess = ReadSlotBytes(SlotName, Bytes);

        FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, SlotName, bReadSuccess, Payload = MoveTemp(Bytes)]() mutable {
            bool bSuccess = false;
            if (bReadSuccess) {
                USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(Payload);
                if (LoadedObject != nullptr && LoadedObject->IsA(WeakThis->SaveGameClass)) {
                    UGameSaveArchive* Archive = CastChecked<UGameSaveArchive>(LoadedObject);
                    bSuccess = Archive->Metadata.ArchiveVersion <= UGameSaveArchive::CurrentArchiveVersion
                        && WeakThis->ApplyArchive(SlotName, *Archive);
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
    if (!SerializeObject(SaveGameObject, Bytes)) {
        GAME_LOG(Error, TEXT("【存档子系统】自定义对象序列化失败: %s"), *SlotName);
        return false;
    }

    const uint64 SaveGeneration = IssueSlotSaveGeneration(SlotName);
    return WriteSlotBytes(SlotName, Bytes, SaveGeneration);
}

bool USaveGameSubsystem::LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad, USaveGame*& OutSaveGame) {
    OutSaveGame = nullptr;
    if (SaveGameClassToLoad == nullptr || !IsSlotNameAllowed(SlotName)) {
        return false;
    }

    TArray<uint8> Bytes;
    if (!ReadSlotBytes(SlotName, Bytes)) {
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
    return FPaths::FileExists(GetSaveSlotFilePath(SlotName)) || FPaths::FileExists(GetSaveSlotTempFilePath(SlotName));
}

bool USaveGameSubsystem::DeleteSaveSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        return false;
    }
    PreservedSegmentsBySlot.Remove(SlotName);
    InvalidateSlotSaveGeneration(SlotName);
    FScopeLock Lock(&GetSlotIoMutex());
    bool bDeleted = false;
    const FString FinalPath = GetSaveSlotFilePath(SlotName);
    const FString TempPath = GetSaveSlotTempFilePath(SlotName);
    if (FPaths::FileExists(FinalPath)) {
        bDeleted |= IFileManager::Get().Delete(*FinalPath);
    }
    if (FPaths::FileExists(TempPath)) {
        bDeleted |= IFileManager::Get().Delete(*TempPath);
    }
    return bDeleted;
}

void USaveGameSubsystem::RegisterSegment(const FGameSaveSegment& Segment) {
    if (Segment.SegmentName.IsNone() || !Segment.Save || !Segment.Load) {
        GAME_LOG(Error, TEXT("【存档子系统】存档段注册被拒：段名、保存委托与加载委托缺一不可。"));
        return;
    }
    Segments.Add(Segment.SegmentName, Segment);
}

bool USaveGameSubsystem::UnregisterSegment(FName SegmentName) {
    return Segments.Remove(SegmentName) > 0;
}

bool USaveGameSubsystem::HasSegment(FName SegmentName) const {
    return Segments.Contains(SegmentName);
}

bool USaveGameSubsystem::BuildArchive(const FString& SlotName, UGameSaveArchive& OutArchive) const {
    OutArchive.Metadata.SlotName = SlotName;
    OutArchive.Metadata.SaveTimestamp = FDateTime::UtcNow();
    OutArchive.Metadata.ArchiveVersion = UGameSaveArchive::CurrentArchiveVersion;
    OutArchive.Segments.Reset();

    if (const TMap<FName, FGameSaveSegmentPayload>* Preserved = PreservedSegmentsBySlot.Find(SlotName)) {
        for (const TPair<FName, FGameSaveSegmentPayload>& Pair : *Preserved) {
            if (!Segments.Contains(Pair.Key)) {
                OutArchive.Segments.Add(Pair.Key, Pair.Value);
            }
        }
    }

    for (const TPair<FName, FGameSaveSegment>& Pair : Segments) {
        FGameSaveSegmentPayload Payload;
        Payload.SegmentVersion = Pair.Value.SegmentVersion;

        FMemoryWriter Writer(Payload.Bytes, true);
        Pair.Value.Save(Writer);
        if (Writer.IsError()) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段保存失败: %s"), *Pair.Key.ToString());
            return false;
        }

        OutArchive.Segments.Add(Pair.Key, MoveTemp(Payload));
    }
    return true;
}

bool USaveGameSubsystem::ApplyArchive(const FString& SlotName, const UGameSaveArchive& Archive) {
    for (const TPair<FName, FGameSaveSegment>& Pair : Segments) {
        const FGameSaveSegmentPayload* Payload = Archive.Segments.Find(Pair.Key);
        if (Payload != nullptr && Payload->SegmentVersion > Pair.Value.SegmentVersion) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段 %s 版本 %d 高于当前段版本 %d，拒绝加载。"),
                *Pair.Key.ToString(), Payload->SegmentVersion, Pair.Value.SegmentVersion);
            return false;
        }
    }

    for (const TPair<FName, FGameSaveSegment>& Pair : Segments) {
        const FGameSaveSegmentPayload* Payload = Archive.Segments.Find(Pair.Key);
        if (Payload == nullptr) {
            GAME_LOG(Warning, TEXT("【存档子系统】存档缺少已注册段，已跳过: %s"), *Pair.Key.ToString());
            continue;
        }

        FMemoryReader Reader(Payload->Bytes, true);
        if (!Pair.Value.Load(Reader, Payload->SegmentVersion) || Reader.IsError()) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段加载失败: %s（落盘版本 %d）"),
                *Pair.Key.ToString(), Payload->SegmentVersion);
            return false;
        }
    }

    const FString TargetSlotKey = !SlotName.IsEmpty() ? SlotName : Archive.Metadata.SlotName;
    if (!TargetSlotKey.IsEmpty()) {
        TMap<FName, FGameSaveSegmentPayload>& Preserved = PreservedSegmentsBySlot.FindOrAdd(TargetSlotKey);
        Preserved.Reset();
        for (const TPair<FName, FGameSaveSegmentPayload>& Pair : Archive.Segments) {
            if (!Segments.Contains(Pair.Key)) {
                Preserved.Add(Pair.Key, Pair.Value);
            }
        }
        if (Preserved.IsEmpty()) {
            PreservedSegmentsBySlot.Remove(TargetSlotKey);
        }
    }
    return true;
}

bool USaveGameSubsystem::SerializeObject(USaveGame* SaveGameObject, TArray<uint8>& OutBytes) {
    return UGameplayStatics::SaveGameToMemory(SaveGameObject, OutBytes);
}

bool USaveGameSubsystem::WriteSlotBytes(const FString& SlotName, const TArray<uint8>& Bytes, uint64 SaveGeneration) {
    FScopeLock Lock(&GetSlotIoMutex());

    if (SaveGeneration > 0) {
        const uint64* Committed = GetCommittedSlotGenerations().Find(SlotName);
        if (Committed != nullptr && *Committed > SaveGeneration) {
            return true;
        }
    }

    const FString FinalPath = GetSaveSlotFilePath(SlotName);
    const FString TempPath = GetSaveSlotTempFilePath(SlotName);

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(FinalPath), true);

    if (!FFileHelper::SaveArrayToFile(Bytes, *TempPath)) {
        IFileManager::Get().Delete(*TempPath);
        return false;
    }

    if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, false, false, false)) {
        return false;
    }

    if (SaveGeneration > 0) {
        GetCommittedSlotGenerations().Add(SlotName, SaveGeneration);
    }

    return true;
}

bool USaveGameSubsystem::ReadSlotBytes(const FString& SlotName, TArray<uint8>& OutBytes) {
    FScopeLock Lock(&GetSlotIoMutex());

    const FString FinalPath = GetSaveSlotFilePath(SlotName);
    const FString TempPath = GetSaveSlotTempFilePath(SlotName);

    if (FPaths::FileExists(FinalPath)) {
        if (FFileHelper::LoadFileToArray(OutBytes, *FinalPath) && HasValidSaveGameHeader(OutBytes, SlotName)) {
            return true;
        }
        GAME_LOG(Warning, TEXT("【存档子系统】正式存档损坏或不完整，尝试自临时归档恢复: %s"), *SlotName);
    }

    if (!FPaths::FileExists(TempPath)) {
        return false;
    }
    if (!FFileHelper::LoadFileToArray(OutBytes, *TempPath) || !HasValidSaveGameHeader(OutBytes, SlotName)) {
        return false;
    }
    IFileManager::Get().Move(*FinalPath, *TempPath, true, false, false, false);
    return true;
}
