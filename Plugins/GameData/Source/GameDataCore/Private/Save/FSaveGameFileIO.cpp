/**
 * @file FSaveGameFileIO.cpp
 * @brief 存档底层文件 I/O 与防断电原子写保护实现
 */

#include "Save/FSaveGameFileIO.h"

#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Logging/GameLogCategories.h"

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

}  // namespace

FString FSaveGameFileIO::GetSaveSlotFilePath(const FString& SlotName) {
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav"));
}

FString FSaveGameFileIO::GetSaveSlotTempFilePath(const FString& SlotName) {
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav.tmp"));
}

bool FSaveGameFileIO::IsSlotNameAllowed(const FString& SlotName, const TSet<FString>& AllowedSlotNames) {
    if (SlotName.IsEmpty() || SlotName.Len() > 64) {
        return false;
    }
    if (SlotName.Contains(TEXT("/")) || SlotName.Contains(TEXT("\\")) || SlotName.Contains(TEXT("."))) {
        return false;
    }
    return AllowedSlotNames.IsEmpty() || AllowedSlotNames.Contains(SlotName);
}

bool FSaveGameFileIO::DoesSaveSlotExist(const FString& SlotName) {
    return FPaths::FileExists(GetSaveSlotFilePath(SlotName)) || FPaths::FileExists(GetSaveSlotTempFilePath(SlotName));
}

bool FSaveGameFileIO::DeleteSaveSlot(const FString& SlotName) {
    InvalidateSlotSaveGeneration(SlotName);
    FScopeLock Lock(&GetSlotIoMutex());

    bool bFoundAnyFile = false;
    bool bDeleteFailed = false;
    const FString FinalPath = GetSaveSlotFilePath(SlotName);
    const FString TempPath = GetSaveSlotTempFilePath(SlotName);

    if (FPaths::FileExists(FinalPath)) {
        bFoundAnyFile = true;
        bDeleteFailed |= !IFileManager::Get().Delete(*FinalPath, false, true);
    }
    if (FPaths::FileExists(TempPath)) {
        bFoundAnyFile = true;
        bDeleteFailed |= !IFileManager::Get().Delete(*TempPath, false, true);
    }
    const bool bDeleted = bFoundAnyFile && !bDeleteFailed;
    if (bDeleted) {
        GAME_LOG(Log, TEXT("【存档文件 I/O】删除存档槽位文件成功: %s。"), *SlotName);
    }
    return bDeleted;
}

bool FSaveGameFileIO::SerializeObject(USaveGame* SaveGameObject, TArray<uint8>& OutBytes) {
    return UGameplayStatics::SaveGameToMemory(SaveGameObject, OutBytes);
}

uint64 FSaveGameFileIO::IssueSlotSaveGeneration(const FString& SlotName) {
    FScopeLock Lock(&GetSlotIoMutex());
    uint64& Generation = GetIssuedSlotGenerations().FindOrAdd(SlotName);
    return ++Generation;
}

void FSaveGameFileIO::InvalidateSlotSaveGeneration(const FString& SlotName) {
    FScopeLock Lock(&GetSlotIoMutex());
    const uint64 CurrentIssued = GetIssuedSlotGenerations().FindOrAdd(SlotName);
    GetCommittedSlotGenerations().Add(SlotName, CurrentIssued);
}

bool FSaveGameFileIO::WriteSlotBytes(const FString& SlotName, const TArray<uint8>& Bytes, uint64 SaveGeneration) {
    FScopeLock Lock(&GetSlotIoMutex());

    if (SaveGeneration > 0) {
        const uint64* Committed = GetCommittedSlotGenerations().Find(SlotName);
        if (Committed != nullptr && *Committed >= SaveGeneration) {
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

    GAME_LOG(Log, TEXT("【存档文件 I/O】原子写入存档槽位成功: 槽位=%s, 字节数=%d, 纪元=%llu。"),
        *SlotName, Bytes.Num(), SaveGeneration);

    return true;
}

bool FSaveGameFileIO::ReadSlotBytes(const FString& SlotName, TArray<uint8>& OutBytes) {
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
        OutBytes.Reset();
        return false;
    }

    if (!FFileHelper::LoadFileToArray(OutBytes, *TempPath) || !HasValidSaveGameHeader(OutBytes, SlotName)) {
        OutBytes.Reset();
        return false;
    }

    if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, true, true, false)) {
        GAME_LOG(Error, TEXT("【存档文件 I/O】临时档恢复换入失败: %s"), *FinalPath);
        OutBytes.Reset();
        return false;
    }
    return true;
}

bool FSaveGameFileIO::HasValidSaveGameHeader(const TArray<uint8>& Bytes, const FString& SlotName) {
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

