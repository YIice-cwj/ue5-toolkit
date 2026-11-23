/**
 * @file FSaveGameFileIO.cpp
 * @brief 存档底层文件 I/O 与防断电原子写保护实现
 */

#include "Save/FSaveGameFileIO.h"

#include "Core/DataErrorCodes.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/GameLogCategories.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Types/TCancelableScopeGuard.h"

namespace {

constexpr int32 MaxSlotNameLength = 64;
constexpr int32 SaveGameFileTypeTag = 0x53415647;
constexpr int32 MinSaveGameHeaderBytes = 64;

/**
 * @brief 内存元数据互斥锁（仅保护代次表与槽位锁表，绝不跨磁盘 I/O 持有；加锁顺序永远晚于槽位 I/O 锁）
 * @return 全局元数据互斥锁引用
 */
FCriticalSection& GetMetadataMutex() {
    static FCriticalSection Mutex;
    return Mutex;
}

/**
 * @brief 获取槽位签发代次映射表（须在持有 GetMetadataMutex() 下访问）
 * @return 槽位名到最高签发代次的映射表引用
 */
TMap<FString, uint64>& GetIssuedSlotGenerations() {
    static TMap<FString, uint64> Generations;
    return Generations;
}

/**
 * @brief 获取槽位已提交高水位代次映射表（须在持有 GetMetadataMutex() 下访问）
 * @return 槽位名到最高提交/失效代次的映射表引用
 */
TMap<FString, uint64>& GetCommittedSlotGenerations() {
    static TMap<FString, uint64> Generations;
    return Generations;
}

/**
 * @brief 获取槽位级磁盘 I/O 互斥锁映射表（须在持有 GetMetadataMutex() 下访问）
 * @return 槽位名到互斥锁共享引用的映射表引用
 */
TMap<FString, TSharedRef<FCriticalSection>>& GetSlotIoMutexMap() {
    static TMap<FString, TSharedRef<FCriticalSection>> SlotMutexes;
    return SlotMutexes;
}

/**
 * @brief 按槽位获取或创建独立的磁盘 I/O 互斥锁（允许不同槽位并发读写，同时避免主线程签发代次被慢盘阻塞）
 * @param SlotName 存档槽位名称
 * @return 槽位级互斥锁共享引用
 */
TSharedRef<FCriticalSection> AcquireSlotIoMutex(const FString& SlotName) {
    FScopeLock MetadataLock(&GetMetadataMutex());
    TMap<FString, TSharedRef<FCriticalSection>>& SlotMutexes = GetSlotIoMutexMap();
    if (const TSharedRef<FCriticalSection>* Found = SlotMutexes.Find(SlotName)) {
        return *Found;
    }
    return SlotMutexes.Emplace(SlotName, MakeShared<FCriticalSection>());
}

/**
 * @brief 尝试获取已存在的槽位级磁盘 I/O 互斥锁（只读探针不创建新锁项，防止随机槽位探测无限增长）
 * @param SlotName 存档槽位名称
 * @return 已存在则返回锁指针，否则返回空指针
 */
TSharedPtr<FCriticalSection> TryGetExistingSlotIoMutex(const FString& SlotName) {
    FScopeLock MetadataLock(&GetMetadataMutex());
    if (const TSharedRef<FCriticalSection>* Found = GetSlotIoMutexMap().Find(SlotName)) {
        return *Found;
    }
    return nullptr;
}

/**
 * @brief 在已持有 GetMetadataMutex() 的前提下递增槽位签发水位并同步推进已提交高水位，使所有历史及在途代次严格失效
 * @param SlotName 存档槽位名称
 */
void InvalidateSlotGenerationLocked(const FString& SlotName) {
    uint64& CurrentIssued = GetIssuedSlotGenerations().FindOrAdd(SlotName);
    uint64& Committed = GetCommittedSlotGenerations().FindOrAdd(SlotName);
    const uint64 NextWatermark = FMath::Max(CurrentIssued, Committed) + 1;
    CurrentIssued = NextWatermark;
    Committed = NextWatermark;
}

/**
 * @brief 检查给定写入代次是否已落后于当前槽位已提交高水位
 * @param SlotName 存档槽位名称
 * @param SaveGeneration 待校验的写入代次
 * @return 过期返回 true
 */
bool IsSaveGenerationStale(const FString& SlotName, uint64 SaveGeneration) {
    if (SaveGeneration == 0) {
        return false;
    }
    FScopeLock MetadataLock(&GetMetadataMutex());
    const uint64* Committed = GetCommittedSlotGenerations().Find(SlotName);
    return Committed != nullptr && *Committed >= SaveGeneration;
}

/**
 * @brief 单调推进槽位已提交代次高水位
 * @param SlotName 存档槽位名称
 * @param SaveGeneration 成功落盘的代次序号
 */
void AdvanceCommittedGeneration(const FString& SlotName, uint64 SaveGeneration) {
    if (SaveGeneration == 0) {
        return;
    }
    FScopeLock MetadataLock(&GetMetadataMutex());
    uint64& Committed = GetCommittedSlotGenerations().FindOrAdd(SlotName);
    if (SaveGeneration > Committed) {
        Committed = SaveGeneration;
    }
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
    if (SlotName.IsEmpty() || SlotName.Len() > MaxSlotNameLength) {
        return false;
    }
    if (FChar::IsWhitespace(SlotName[0]) || FChar::IsWhitespace(SlotName[SlotName.Len() - 1])) {
        return false;
    }
    for (const TCHAR Ch : SlotName) {
        if (static_cast<uint32>(Ch) < 0x20u || static_cast<uint32>(Ch) == 0x7Fu) {
            return false;
        }
        switch (Ch) {
            case TEXT('/'):
            case TEXT('\\'):
            case TEXT('.'):
            case TEXT(':'):
            case TEXT('*'):
            case TEXT('?'):
            case TEXT('"'):
            case TEXT('<'):
            case TEXT('>'):
            case TEXT('|'):
                return false;
            default:
                break;
        }
    }
    return AllowedSlotNames.IsEmpty() || AllowedSlotNames.Contains(SlotName);
}

/**
 * @brief 检查指定槽位的磁盘物理文件是否存在（正式文件或临时文件）
 * @param SlotName 存档槽位名称
 * @return 存在任意对应存档物理文件返回 true，否则返回 false
 */
bool FSaveGameFileIO::DoesSaveSlotExist(const FString& SlotName) {
    if (const TSharedPtr<FCriticalSection> ExistingMutex = TryGetExistingSlotIoMutex(SlotName)) {
        FScopeLock SlotIoLock(ExistingMutex.Get());
        return FPaths::FileExists(GetSaveSlotFilePath(SlotName)) || FPaths::FileExists(GetSaveSlotTempFilePath(SlotName));
    }
    return FPaths::FileExists(GetSaveSlotFilePath(SlotName)) || FPaths::FileExists(GetSaveSlotTempFilePath(SlotName));
}

/**
 * @brief 原子失效在途写入代次并删除指定槽位的正式文件与临时文件（同锁内防止写穿与 ABA 复活）
 * @param SlotName 存档槽位名称
 * @return 成功删除返回 Success，未找到对应文件或删除失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> FSaveGameFileIO::DeleteSaveSlot(const FString& SlotName) {
    const TSharedRef<FCriticalSection> SlotMutex = AcquireSlotIoMutex(SlotName);
    FScopeLock SlotIoLock(&SlotMutex.Get());

    {
        FScopeLock MetadataLock(&GetMetadataMutex());
        InvalidateSlotGenerationLocked(SlotName);
    }

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

    if (bFoundAnyFile && !bDeleteFailed) {
        GAME_LOG(Log, TEXT("【存档文件 I/O】删除存档槽位文件成功: %s。"), *SlotName);
        return TGameStatusResult<void>::Success();
    }

    const int32 ErrorCode = bFoundAnyFile ? Game::DataErrorCodes::DeleteSlotFailed : Game::DataErrorCodes::SaveSlotNotFound;
    return TGameStatusResult<void>::Failure(
        Game::ModuleId::Data,
        ErrorCode,
        FString::Printf(TEXT("删除槽位失败或槽位不存在: %s"), *SlotName)
    );
}

/**
 * @brief 将 USaveGame 对象序列化为二进制内存字节数组
 * @param SaveGameObject 待序列化的存档对象指针
 * @return 成功返回二进制字节数组，对象无效或序列化异常返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<TArray<uint8>> FSaveGameFileIO::SerializeObject(USaveGame* SaveGameObject) {
    if (SaveGameObject == nullptr) {
        return TGameStatusResult<TArray<uint8>>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSaveObject,
            TEXT("SaveGameObject 为空")
        );
    }
    TArray<uint8> Bytes;
    if (!UGameplayStatics::SaveGameToMemory(SaveGameObject, Bytes)) {
        return TGameStatusResult<TArray<uint8>>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::SaveSerializeFailed,
            TEXT("序列化存档数据到内存失败")
        );
    }
    return TGameStatusResult<TArray<uint8>>::Success(MoveTemp(Bytes));
}

/**
 * @brief 生成并签发槽位递增的写入代次序号（仅锁内存代次表，不阻塞于后台磁盘 I/O）
 * @param SlotName 存档槽位名称
 * @return 本次签发的唯一代次序号；删除后仍沿高水位严格递增，不复用历史代次
 */
uint64 FSaveGameFileIO::IssueSlotSaveGeneration(const FString& SlotName) {
    FScopeLock MetadataLock(&GetMetadataMutex());
    uint64& Generation = GetIssuedSlotGenerations().FindOrAdd(SlotName);
    return ++Generation;
}

/**
 * @brief 使槽位当前已签发的全部在途保存代次失效（单调推进签发与已提交高水位）
 * @param SlotName 存档槽位名称
 */
void FSaveGameFileIO::InvalidateSlotSaveGeneration(const FString& SlotName) {
    const TSharedRef<FCriticalSection> SlotMutex = AcquireSlotIoMutex(SlotName);
    FScopeLock SlotIoLock(&SlotMutex.Get());
    FScopeLock MetadataLock(&GetMetadataMutex());
    InvalidateSlotGenerationLocked(SlotName);
}

/**
 * @brief 检查给定写入代次是否已被该槽位更新的签发代次或删槽失效水位超越
 * @param SlotName 存档槽位名称
 * @param SaveGeneration 待校验的写入代次序号
 * @return 已被更新写入或删槽超越返回 true
 */
bool FSaveGameFileIO::IsSlotSaveGenerationSuperseded(const FString& SlotName, uint64 SaveGeneration) {
    if (SaveGeneration == 0) {
        return false;
    }
    FScopeLock MetadataLock(&GetMetadataMutex());
    const uint64* Issued = GetIssuedSlotGenerations().Find(SlotName);
    const uint64* Committed = GetCommittedSlotGenerations().Find(SlotName);
    const uint64 MaxIssued = Issued != nullptr ? *Issued : 0;
    const uint64 MaxCommitted = Committed != nullptr ? *Committed : 0;
    return MaxIssued > SaveGeneration || MaxCommitted > SaveGeneration;
}

/**
 * @brief 按槽位加锁执行原子落盘写入（先写 .tmp 临时文件，再原子替换正式文件；Move 失败保留有效 .tmp 供灾备恢复）
 * @param SlotName 存档槽位名称
 * @param Bytes 待落盘的二进制字节数组
 * @param SaveGeneration 签发的代次序号（非 0 时校验高水位过期性，过期写安全跳过并返回 Success）
 * @param bOutSkippedStale 可选输出：因代次落后于已提交高水位而跳过物理写盘时置为 true，实际完成写盘置为 false
 * @return 成功或过期安全跳过返回 Success，I/O 失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> FSaveGameFileIO::WriteSlotBytes(
    const FString& SlotName,
    const TArray<uint8>& Bytes,
    uint64 SaveGeneration,
    bool* bOutSkippedStale
) {
    if (bOutSkippedStale != nullptr) {
        *bOutSkippedStale = false;
    }
    const TSharedRef<FCriticalSection> SlotMutex = AcquireSlotIoMutex(SlotName);
    FScopeLock SlotIoLock(&SlotMutex.Get());

    if (IsSaveGenerationStale(SlotName, SaveGeneration)) {
        if (bOutSkippedStale != nullptr) {
            *bOutSkippedStale = true;
        }
        GAME_LOG(Verbose, TEXT("【存档文件 I/O】已跳过过期代次写入: 槽位=%s, 纪元=%llu。"), *SlotName, SaveGeneration);
        return TGameStatusResult<void>::Success();
    }

    const FString FinalPath = GetSaveSlotFilePath(SlotName);
    const FString TempPath = GetSaveSlotTempFilePath(SlotName);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(FinalPath), true);

    auto TempCleanupGuard = MakeCancelableScopeGuard([&TempPath]() {
        IFileManager::Get().Delete(*TempPath, false, true);
    });

    if (!FFileHelper::SaveArrayToFile(Bytes, *TempPath)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::WriteTempFileFailed,
            FString::Printf(TEXT("写入临时存档文件失败: %s"), *TempPath)
        );
    }

    TempCleanupGuard.Dismiss();

    if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, true, false, false)) {
        GAME_LOG(Error, TEXT("【存档文件 I/O】原子换入正式档失败，已保留有效临时档供灾备恢复: %s"), *TempPath);
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::CommitTempFileFailed,
            FString::Printf(TEXT("移动临时存档到正式路径失败: %s"), *FinalPath)
        );
    }

    AdvanceCommittedGeneration(SlotName, SaveGeneration);
    GAME_LOG(Log, TEXT("【存档文件 I/O】原子写入存档槽位成功: 槽位=%s, 字节数=%d, 纪元=%llu。"),
        *SlotName, Bytes.Num(), SaveGeneration);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 读取指定槽位的二进制字节（正式文件损坏或缺失时自动自 .tmp 临时文件抢救恢复）
 * @param SlotName 存档槽位名称
 * @return 成功返回包含二进制数组的 StatusResult，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<TArray<uint8>> FSaveGameFileIO::ReadSlotBytes(const FString& SlotName) {
    const TSharedRef<FCriticalSection> SlotMutex = AcquireSlotIoMutex(SlotName);
    FScopeLock SlotIoLock(&SlotMutex.Get());

    const FString FinalPath = GetSaveSlotFilePath(SlotName);
    const FString TempPath = GetSaveSlotTempFilePath(SlotName);
    TArray<uint8> OutBytes;

    bool bFinalCorrupted = false;
    if (FPaths::FileExists(FinalPath)) {
        if (FFileHelper::LoadFileToArray(OutBytes, *FinalPath) && HasValidSaveGameHeader(OutBytes, SlotName)) {
            return TGameStatusResult<TArray<uint8>>::Success(MoveTemp(OutBytes));
        }
        bFinalCorrupted = true;
        OutBytes.Reset();
        GAME_LOG(Warning, TEXT("【存档子系统】正式存档损坏或不完整，尝试自临时归档恢复: %s"), *SlotName);
    }

    if (!FPaths::FileExists(TempPath)) {
        return TGameStatusResult<TArray<uint8>>::Failure(
            Game::ModuleId::Data,
            bFinalCorrupted ? Game::DataErrorCodes::CorruptedSaveFile : Game::DataErrorCodes::SaveSlotNotFound,
            bFinalCorrupted
                ? FString::Printf(TEXT("正式存档已损坏且无可用临时备份: %s"), *SlotName)
                : FString::Printf(TEXT("未找到存档槽位文件: %s"), *SlotName)
        );
    }

    if (!FFileHelper::LoadFileToArray(OutBytes, *TempPath) || !HasValidSaveGameHeader(OutBytes, SlotName)) {
        OutBytes.Reset();
        return TGameStatusResult<TArray<uint8>>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::CorruptedSaveFile,
            FString::Printf(TEXT("存档槽位临时文件已损坏: %s"), *SlotName)
        );
    }

    if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, true, false, false)) {
        OutBytes.Reset();
        GAME_LOG(Error, TEXT("【存档文件 I/O】临时档恢复换入失败: %s"), *FinalPath);
        return TGameStatusResult<TArray<uint8>>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::RecoveryMoveFailed,
            FString::Printf(TEXT("恢复临时存档到正式槽位失败: %s"), *FinalPath)
        );
    }
    return TGameStatusResult<TArray<uint8>>::Success(MoveTemp(OutBytes));
}

/**
 * @brief 校验二进制前导魔数（0x53415647）与最小头长度（>= 64 字节）是否符合 UE USaveGame 格式
 * @param Bytes 待校验的二进制字节数组
 * @param SlotName 存档槽位名称（用于诊断日志）
 * @return 前导魔数与最小长度校验通过返回 true，否则返回 false
 */
bool FSaveGameFileIO::HasValidSaveGameHeader(const TArray<uint8>& Bytes, const FString& SlotName) {
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
