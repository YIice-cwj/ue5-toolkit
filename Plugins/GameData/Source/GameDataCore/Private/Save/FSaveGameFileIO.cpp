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

/**
 * @brief 计算并获取指定槽位的正式存档文件绝对路径
 * @param SlotName 存档槽位名称
 * @return 正式存档文件的绝对路径字符串
 */
FString FSaveGameFileIO::GetSaveSlotFilePath(const FString& SlotName) {
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav"));
}

/**
 * @brief 计算并获取指定槽位的临时写入归档文件绝对路径（.sav.tmp）
 * @param SlotName 存档槽位名称
 * @return 临时写入文件的绝对路径字符串
 */
FString FSaveGameFileIO::GetSaveSlotTempFilePath(const FString& SlotName) {
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav.tmp"));
}

/**
 * @brief 槽位名称合法性与白名单校验（防止目录穿越与非法文件名）
 * @param SlotName 待校验的存档槽位名称
 * @param AllowedSlotNames 白名单集合（若为空集合则仅校验路径合法字符）
 * @return 合法通过返回 true，否则返回 false
 */
bool FSaveGameFileIO::IsSlotNameAllowed(const FString& SlotName, const TSet<FString>& AllowedSlotNames) {
    if (SlotName.IsEmpty() || SlotName.Len() > 64) {
        return false;
    }
    if (SlotName.Contains(TEXT("/")) || SlotName.Contains(TEXT("\\")) || SlotName.Contains(TEXT("."))) {
        return false;
    }
    return AllowedSlotNames.IsEmpty() || AllowedSlotNames.Contains(SlotName);
}

/**
 * @brief 检查指定槽位的磁盘物理文件是否存在（正式文件或临时文件）
 * @param SlotName 存档槽位名称
 * @return 存在任意对应存档物理文件返回 true，否则返回 false
 */
bool FSaveGameFileIO::DoesSaveSlotExist(const FString& SlotName) {
    return FPaths::FileExists(GetSaveSlotFilePath(SlotName)) || FPaths::FileExists(GetSaveSlotTempFilePath(SlotName));
}

/**
 * @brief 删除指定槽位的正式文件与临时文件，并写入保存代次删除墓碑
 * @param SlotName 存档槽位名称
 * @return 成功删除返回 Success，未找到对应文件或删除失败返回 Failure
 */
TGameStatusResult<void> FSaveGameFileIO::DeleteSaveSlot(const FString& SlotName) {
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
        return TGameStatusResult<void>::Success();
    }
    return TGameStatusResult<void>::Failure(1, FString::Printf(TEXT("删除槽位失败或槽位不存在: %s"), *SlotName));
}

/**
 * @brief 将 USaveGame 对象序列化为二进制内存字节数组
 * @param SaveGameObject 待序列化的存档对象指针
 * @return 成功返回二进制字节数组，对象无效或序列化异常返回 Failure
 */
TGameResult<TArray<uint8>, FGameError> FSaveGameFileIO::SerializeObject(USaveGame* SaveGameObject) {
    if (SaveGameObject == nullptr) {
        return TGameResult<TArray<uint8>, FGameError>::Failure(FGameError(TEXT("SaveGameObject 为空")));
    }
    TArray<uint8> Bytes;
    if (!UGameplayStatics::SaveGameToMemory(SaveGameObject, Bytes)) {
        return TGameResult<TArray<uint8>, FGameError>::Failure(FGameError(TEXT("序列化存档数据到内存失败")));
    }
    return TGameResult<TArray<uint8>, FGameError>::Success(MoveTemp(Bytes));
}

/**
 * @brief 生成并签发槽位递增的写入代次序号（防止异步并发保存乱序覆写）
 * @param SlotName 存档槽位名称
 * @return 本次签发的唯一代次序号；删除后仍沿高水位严格递增，不复用历史代次
 */
uint64 FSaveGameFileIO::IssueSlotSaveGeneration(const FString& SlotName) {
    FScopeLock Lock(&GetSlotIoMutex());
    uint64& Generation = GetIssuedSlotGenerations().FindOrAdd(SlotName);
    return ++Generation;
}

/**
 * @brief 使槽位当前已签发的全部在途保存代次失效（将已提交高水位推至当前签发水位）
 * @param SlotName 存档槽位名称
 */
void FSaveGameFileIO::InvalidateSlotSaveGeneration(const FString& SlotName) {
    FScopeLock Lock(&GetSlotIoMutex());
    const uint64 CurrentIssued = GetIssuedSlotGenerations().FindOrAdd(SlotName);
    GetCommittedSlotGenerations().Add(SlotName, CurrentIssued);
}

/**
 * @brief 原子安全落盘写入字节（先写 .tmp 临时文件，校验成功后原子替换正式文件）
 * @param SlotName 存档槽位名称
 * @param Bytes 待落盘的二进制字节数组
 * @param SaveGeneration 签发的代次序号（非 0 时校验过期性）
 * @return 成功返回 Success，失败返回含错误描述的 Failure
 */
TGameStatusResult<void> FSaveGameFileIO::WriteSlotBytes(const FString& SlotName, const TArray<uint8>& Bytes, uint64 SaveGeneration) {
    FScopeLock Lock(&GetSlotIoMutex());

    if (SaveGeneration > 0) {
        const uint64* Committed = GetCommittedSlotGenerations().Find(SlotName);
        if (Committed != nullptr && *Committed >= SaveGeneration) {
            return TGameStatusResult<void>::Success();
        }
    }

    const FString FinalPath = GetSaveSlotFilePath(SlotName);
    const FString TempPath = GetSaveSlotTempFilePath(SlotName);

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(FinalPath), true);

    if (!FFileHelper::SaveArrayToFile(Bytes, *TempPath)) {
        IFileManager::Get().Delete(*TempPath, false, true);
        return TGameStatusResult<void>::Failure(1, FString::Printf(TEXT("写入临时存档文件失败: %s"), *TempPath));
    }

    if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, false, false, false)) {
        IFileManager::Get().Delete(*TempPath, false, true);
        return TGameStatusResult<void>::Failure(2, FString::Printf(TEXT("移动临时存档到正式路径失败: %s"), *FinalPath));
    }

    if (SaveGeneration > 0) {
        GetCommittedSlotGenerations().Add(SlotName, SaveGeneration);
    }

    GAME_LOG(Log, TEXT("【存档文件 I/O】原子写入存档槽位成功: 槽位=%s, 字节数=%d, 纪元=%llu。"),
        *SlotName, Bytes.Num(), SaveGeneration);

    return TGameStatusResult<void>::Success();
}

/**
 * @brief 读取指定槽位的二进制字节（正式文件损坏或缺失时自动自 .tmp 临时文件抢救恢复）
 * @param SlotName 存档槽位名称
 * @return 成功返回包含二进制数组的 Result，失败返回含错误描述的 Failure
 */
TGameResult<TArray<uint8>, FGameError> FSaveGameFileIO::ReadSlotBytes(const FString& SlotName) {
    FScopeLock Lock(&GetSlotIoMutex());

    const FString FinalPath = GetSaveSlotFilePath(SlotName);
    const FString TempPath = GetSaveSlotTempFilePath(SlotName);
    TArray<uint8> OutBytes;

    if (FPaths::FileExists(FinalPath)) {
        if (FFileHelper::LoadFileToArray(OutBytes, *FinalPath) && HasValidSaveGameHeader(OutBytes, SlotName)) {
            return TGameResult<TArray<uint8>, FGameError>::Success(MoveTemp(OutBytes));
        }
        GAME_LOG(Warning, TEXT("【存档子系统】正式存档损坏或不完整，尝试自临时归档恢复: %s"), *SlotName);
    }

    if (!FPaths::FileExists(TempPath)) {
        return TGameResult<TArray<uint8>, FGameError>::Failure(FGameError(FString::Printf(TEXT("未找到存档槽位文件: %s"), *SlotName)));
    }

    if (!FFileHelper::LoadFileToArray(OutBytes, *TempPath) || !HasValidSaveGameHeader(OutBytes, SlotName)) {
        return TGameResult<TArray<uint8>, FGameError>::Failure(FGameError(FString::Printf(TEXT("存档槽位临时文件已损坏: %s"), *SlotName)));
    }

    if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, true, true, false)) {
        GAME_LOG(Error, TEXT("【存档文件 I/O】临时档恢复换入失败: %s"), *FinalPath);
        return TGameResult<TArray<uint8>, FGameError>::Failure(FGameError(FString::Printf(TEXT("恢复临时存档到正式槽位失败: %s"), *FinalPath)));
    }
    return TGameResult<TArray<uint8>, FGameError>::Success(MoveTemp(OutBytes));
}

/**
 * @brief 校验二进制头部魔数与结构是否为合法的 USaveGame 格式
 * @param Bytes 待校验的二进制字节数组
 * @param SlotName 存档槽位名称（用于诊断日志）
 * @return 格式合法有效返回 true，否则返回 false
 */
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
