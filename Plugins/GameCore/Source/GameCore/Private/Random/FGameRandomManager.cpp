/**
 * @file FGameRandomManager.cpp
 * @brief 纯 C++ 多通道隔离随机数流管理器实现
 */

#include "Random/FGameRandomManager.h"

#include "Misc/Crc.h"

/**
 * @brief 深拷贝构造函数
 * @param Other 源管理器
 */
FGameRandomManager::FGameRandomManager(const FGameRandomManager& Other) {
    ChannelStreams.Reserve(Other.ChannelStreams.Num());
    for (const TPair<FName, TSharedRef<FGameRandomStream>>& Pair : Other.ChannelStreams) {
        ChannelStreams.Add(Pair.Key, MakeShared<FGameRandomStream>(*Pair.Value));
    }
}

/**
 * @brief 深拷贝赋值运算符
 * @param Other 源管理器
 * @return 当前管理器引用
 */
FGameRandomManager& FGameRandomManager::operator=(const FGameRandomManager& Other) {
    if (this != &Other) {
        ChannelStreams.Empty(Other.ChannelStreams.Num());
        for (const TPair<FName, TSharedRef<FGameRandomStream>>& Pair : Other.ChannelStreams) {
            ChannelStreams.Add(Pair.Key, MakeShared<FGameRandomStream>(*Pair.Value));
        }
    }
    return *this;
}

/**
 * @brief 获取或惰性创建指定通道的独立随机流
 * @param ChannelName 业务通道名
 * @return 随机流引用
 */
FGameRandomStream& FGameRandomManager::GetOrCreateStream(const FName& ChannelName) {
    return *GetOrCreateStreamShared(ChannelName);
}

/**
 * @brief 获取或惰性创建指定通道的独立随机流共享引用句柄
 * @param ChannelName 业务通道名
 * @return 随机流共享引用句柄
 */
TSharedRef<FGameRandomStream> FGameRandomManager::GetOrCreateStreamShared(const FName& ChannelName) {
    const FName Normalized = NormalizeChannelName(ChannelName);
    if (const TSharedRef<FGameRandomStream>* Existing = ChannelStreams.Find(Normalized)) {
        return *Existing;
    }
    TSharedRef<FGameRandomStream> Created = MakeShared<FGameRandomStream>(DeriveChannelDefaultSeed(Normalized));
    ChannelStreams.Add(Normalized, Created);
    return Created;
}

/**
 * @brief 使用显式种子初始化（或重置）指定通道的随机流
 * @param ChannelName 业务通道名
 * @param Seed 64 位初始种子
 * @return 随机流引用
 */
FGameRandomStream& FGameRandomManager::InitializeStream(const FName& ChannelName, uint64 Seed) {
    const FName Normalized = NormalizeChannelName(ChannelName);
    if (const TSharedRef<FGameRandomStream>* Existing = ChannelStreams.Find(Normalized)) {
        (*Existing)->Initialize(Seed);
        return **Existing;
    }
    TSharedRef<FGameRandomStream> Created = MakeShared<FGameRandomStream>(Seed);
    ChannelStreams.Add(Normalized, Created);
    return *Created;
}

/**
 * @brief 检查指定通道是否已存在
 * @param ChannelName 业务通道名
 * @return 存在返回 true
 */
bool FGameRandomManager::HasStream(const FName& ChannelName) const {
    return ChannelStreams.Contains(NormalizeChannelName(ChannelName));
}

/**
 * @brief 移除指定通道的随机流
 * @param ChannelName 业务通道名
 * @return 存在并成功移除返回 true
 */
bool FGameRandomManager::RemoveStream(const FName& ChannelName) {
    return ChannelStreams.Remove(NormalizeChannelName(ChannelName)) > 0;
}

/**
 * @brief 清空全部已注册的随机流通道
 */
void FGameRandomManager::ClearAllStreams() {
    ChannelStreams.Empty();
}

/**
 * @brief 将所有通道随机流内部状态按确定性字典序导出至二进制归档
 * @param Ar 写入归档器
 */
void FGameRandomManager::ExportStreams(FArchive& Ar) const {
    uint32 Magic = ARCHIVE_MAGIC;
    uint32 Version = ARCHIVE_VERSION;
    int32 Count = ChannelStreams.Num();
    Ar << Magic;
    Ar << Version;
    Ar << Count;

    TArray<FName> SortedKeys;
    ChannelStreams.GetKeys(SortedKeys);
    SortedKeys.Sort(FNameLexicalLess());

    for (const FName& Key : SortedKeys) {
        FString KeyText = Key.ToString();
        uint64 State = ChannelStreams.FindChecked(Key)->GetState();
        Ar << KeyText;
        Ar << State;
    }
}

/**
 * @brief 从二进制归档原子校验并恢复所有通道随机流状态
 * @param Ar 读取归档器
 * @return 恢复成功返回 true
 */
bool FGameRandomManager::ImportStreams(FArchive& Ar) {
    uint32 Magic = 0u;
    uint32 Version = 0u;
    int32 Count = 0;
    Ar << Magic;
    Ar << Version;
    Ar << Count;

    if (Ar.IsError() || Magic != ARCHIVE_MAGIC || Version != ARCHIVE_VERSION || Count < 0 || Count > MAX_IMPORT_STREAMS) {
        return false;
    }

    TMap<FName, TSharedRef<FGameRandomStream>> ImportedMap;
    ImportedMap.Reserve(Count);

    for (int32 Index = 0; Index < Count; ++Index) {
        FString KeyText;
        uint64 State = 0ULL;
        Ar << KeyText;
        Ar << State;
        if (Ar.IsError() || KeyText.IsEmpty()) {
            return false;
        }
        const FName NormalizedKey = NormalizeChannelName(FName(*KeyText));
        if (ImportedMap.Contains(NormalizedKey)) {
            return false;
        }
        TSharedRef<FGameRandomStream> RestoredStream = MakeShared<FGameRandomStream>();
        RestoredStream->SetState(State);
        ImportedMap.Add(NormalizedKey, RestoredStream);
    }

    ChannelStreams = MoveTemp(ImportedMap);
    return true;
}

/**
 * @brief 规范化通道名称（None 转换为 Default）
 * @param ChannelName 待规范化通道名称
 * @return 规范化后的通道名称
 */
FName FGameRandomManager::NormalizeChannelName(const FName& ChannelName) noexcept {
    static const FName DefaultChannelName(TEXT("Default"));
    return ChannelName.IsNone() ? DefaultChannelName : ChannelName;
}

/**
 * @brief 基于规范化通道名派生跨进程一致的 64 位确定性初始种子
 * @param NormalizedName 规范化名称
 * @return 64 位初始种子
 */
uint64 FGameRandomManager::DeriveChannelDefaultSeed(const FName& NormalizedName) {
    const FString NameString = NormalizedName.ToString().ToLower();
    const uint32 CrcLow = FCrc::StrCrc32(*NameString);
    const uint32 CrcHigh = FCrc::StrCrc32(*NameString, CrcLow ^ 0xA5A5A5A5u);
    return (static_cast<uint64>(CrcHigh) << 32u) | static_cast<uint64>(CrcLow);
}
