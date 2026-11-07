/**
 * @file URandomSubsystem.cpp
 * @brief URandomSubsystem 世界级多通道隔离随机数子系统实现
 */

#include "Random/URandomSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/Crc.h"

void URandomSubsystem::Deinitialize() {
    ChannelStreams.Empty();
    Super::Deinitialize();
}

FGameRandomStream& URandomSubsystem::GetOrCreateStream(const FName& ChannelName) {
    const FName Normalized = NormalizeChannelName(ChannelName);
    if (FGameRandomStream* Existing = ChannelStreams.Find(Normalized)) {
        return *Existing;
    }
    return ChannelStreams.Emplace(Normalized, FGameRandomStream(DeriveChannelDefaultSeed(Normalized)));
}

FGameRandomStream& URandomSubsystem::InitializeStream(const FName& ChannelName, uint64 Seed) {
    const FName Normalized = NormalizeChannelName(ChannelName);
    FGameRandomStream& Stream = ChannelStreams.FindOrAdd(Normalized);
    Stream.Initialize(Seed);
    return Stream;
}

bool URandomSubsystem::HasStream(const FName& ChannelName) const {
    return ChannelStreams.Contains(NormalizeChannelName(ChannelName));
}

bool URandomSubsystem::RemoveStream(const FName& ChannelName) {
    return ChannelStreams.Remove(NormalizeChannelName(ChannelName)) > 0;
}

void URandomSubsystem::ClearAllStreams() {
    ChannelStreams.Empty();
}

void URandomSubsystem::ExportStreams(FArchive& Ar) const {
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
        uint64 State = ChannelStreams.FindChecked(Key).GetState();
        Ar << KeyText;
        Ar << State;
    }
}

bool URandomSubsystem::ImportStreams(FArchive& Ar) {
    uint32 Magic = 0u;
    uint32 Version = 0u;
    int32 Count = 0;
    Ar << Magic;
    Ar << Version;
    Ar << Count;

    if (Ar.IsError() || Magic != ARCHIVE_MAGIC || Version != ARCHIVE_VERSION || Count < 0 || Count > MAX_IMPORT_STREAMS) {
        return false;
    }

    TMap<FName, FGameRandomStream> ImportedMap;
    ImportedMap.Reserve(Count);

    for (int32 Index = 0; Index < Count; ++Index) {
        FString KeyText;
        uint64 State = 0ULL;
        Ar << KeyText;
        Ar << State;
        if (Ar.IsError() || KeyText.IsEmpty()) {
            return false;
        }
        FGameRandomStream RestoredStream;
        RestoredStream.SetState(State);
        ImportedMap.Add(FName(*KeyText), RestoredStream);
    }

    ChannelStreams = MoveTemp(ImportedMap);
    return true;
}

URandomSubsystem& URandomSubsystem::Get(const UObject* WorldContextObject) {
    URandomSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

URandomSubsystem* URandomSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<URandomSubsystem>();
    }
    return nullptr;
}

FName URandomSubsystem::NormalizeChannelName(const FName& ChannelName) noexcept {
    static const FName DefaultChannelName(TEXT("Default"));
    return ChannelName.IsNone() ? DefaultChannelName : ChannelName;
}

uint64 URandomSubsystem::DeriveChannelDefaultSeed(const FName& NormalizedName) {
    const FString NameString = NormalizedName.ToString();
    const uint32 CrcLow = FCrc::StrCrc32(*NameString);
    const uint32 CrcHigh = FCrc::StrCrc32(*NameString, CrcLow ^ 0xA5A5A5A5u);
    return (static_cast<uint64>(CrcHigh) << 32u) | static_cast<uint64>(CrcLow);
}
