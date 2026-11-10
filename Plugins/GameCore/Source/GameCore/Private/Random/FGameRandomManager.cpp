/**
 * @file FGameRandomManager.cpp
 * @brief 纯 C++ 多通道隔离随机数流管理器实现
 */

#include "Random/FGameRandomManager.h"

#include "Misc/Crc.h"

FGameRandomManager::FGameRandomManager(const FGameRandomManager& Other) {
    ChannelStreams.Reserve(Other.ChannelStreams.Num());
    for (const TPair<FName, TSharedRef<FGameRandomStream>>& Pair : Other.ChannelStreams) {
        ChannelStreams.Add(Pair.Key, MakeShared<FGameRandomStream>(*Pair.Value));
    }
}

FGameRandomManager& FGameRandomManager::operator=(const FGameRandomManager& Other) {
    if (this != &Other) {
        ChannelStreams.Empty(Other.ChannelStreams.Num());
        for (const TPair<FName, TSharedRef<FGameRandomStream>>& Pair : Other.ChannelStreams) {
            ChannelStreams.Add(Pair.Key, MakeShared<FGameRandomStream>(*Pair.Value));
        }
    }
    return *this;
}

FGameRandomStream& FGameRandomManager::GetOrCreateStream(const FName& ChannelName) {
    return *GetOrCreateStreamShared(ChannelName);
}

TSharedRef<FGameRandomStream> FGameRandomManager::GetOrCreateStreamShared(const FName& ChannelName) {
    const FName Normalized = NormalizeChannelName(ChannelName);
    if (const TSharedRef<FGameRandomStream>* Existing = ChannelStreams.Find(Normalized)) {
        return *Existing;
    }
    TSharedRef<FGameRandomStream> Created = MakeShared<FGameRandomStream>(DeriveChannelDefaultSeed(Normalized));
    ChannelStreams.Add(Normalized, Created);
    return Created;
}

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

bool FGameRandomManager::HasStream(const FName& ChannelName) const {
    return ChannelStreams.Contains(NormalizeChannelName(ChannelName));
}

bool FGameRandomManager::RemoveStream(const FName& ChannelName) {
    return ChannelStreams.Remove(NormalizeChannelName(ChannelName)) > 0;
}

void FGameRandomManager::ClearAllStreams() {
    ChannelStreams.Empty();
}

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

FName FGameRandomManager::NormalizeChannelName(const FName& ChannelName) noexcept {
    static const FName DefaultChannelName(TEXT("Default"));
    return ChannelName.IsNone() ? DefaultChannelName : ChannelName;
}

uint64 FGameRandomManager::DeriveChannelDefaultSeed(const FName& NormalizedName) {
    const FString NameString = NormalizedName.ToString().ToLower();
    const uint32 CrcLow = FCrc::StrCrc32(*NameString);
    const uint32 CrcHigh = FCrc::StrCrc32(*NameString, CrcLow ^ 0xA5A5A5A5u);
    return (static_cast<uint64>(CrcHigh) << 32u) | static_cast<uint64>(CrcLow);
}
