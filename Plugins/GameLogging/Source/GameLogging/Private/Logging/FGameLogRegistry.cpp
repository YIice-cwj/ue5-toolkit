// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/FGameLogRegistry.h"

#include <atomic>
#include "Containers/Array.h"
#include "Misc/CString.h"
#include "Misc/ScopeRWLock.h"
#include "Logging/GameLogCategories.h"

namespace Game::Log {

namespace {

struct FModuleLogEntry {
public:
    /**
     * @brief 判断登记项是否匹配指定模块名
     * @param InModuleName 已校验的非空模块名
     * @return 名称完全相同时返回 true
     */
    [[nodiscard]] bool Matches(const char* InModuleName) const {
        return !Name.IsEmpty() && FCStringAnsi::Strcmp(Name.GetData(), InModuleName) == 0;
    }

public:
    TArray<ANSICHAR> Name;
    FLogCategoryBase* Category = nullptr;
};

struct FRegistryStorage {
public:
    /**
     * @brief 获取常驻进程生命周期的注册表存储（防退出期静态逆序析构踩踏）
     * @return 注册表存储单例引用
     */
    static FRegistryStorage& Get() {
        static FRegistryStorage* Instance = new FRegistryStorage();
        return *Instance;
    }

public:
    FRWLock Lock;
    TArray<FModuleLogEntry> Entries;
    std::atomic<int32> Epoch{0};
};

} // namespace

void FGameLogRegistry::RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory) {
    RegisterScopedCategory(InModuleName, InCategory);
}

void FGameLogRegistry::UnregisterCategory(const char* InModuleName) {
    if (!InModuleName || InModuleName[0] == '\0') {
        return;
    }
    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);
    if (Storage.Entries.RemoveAll([InModuleName](const FModuleLogEntry& Entry) { return Entry.Matches(InModuleName); }) > 0) {
        Storage.Epoch.fetch_add(1, std::memory_order_release);
    }
}

FLogCategoryBase& FGameLogRegistry::GetCategory(const char* InModuleName) {
    if (!InModuleName || InModuleName[0] == '\0') {
        return GetFallbackCategory();
    }

    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock ReadLock(Storage.Lock, SLT_ReadOnly);
    for (const FModuleLogEntry& Entry : Storage.Entries) {
        if (Entry.Matches(InModuleName)) {
            return *Entry.Category;
        }
    }
    return GetFallbackCategory();
}

FLogCategoryBase& FGameLogRegistry::GetFallbackCategory() {
#if NO_LOGGING
    static FLogCategoryBase NoLoggingFallback(TEXT("LogGame"), ELogVerbosity::Log, ELogVerbosity::All);
    return NoLoggingFallback;
#else
    return LogGame;
#endif
}

int32 FGameLogRegistry::GetEpoch() noexcept {
    return FRegistryStorage::Get().Epoch.load(std::memory_order_acquire);
}

TArray<ANSICHAR> FGameLogRegistry::MakeAnsiName(const char* InName) {
    const int32 Length = FCStringAnsi::Strlen(InName);
    TArray<ANSICHAR> Result;
    Result.SetNumUninitialized(Length + 1);
    FMemory::Memcpy(Result.GetData(), InName, Length + 1);
    return Result;
}

FLogCategoryBase* FGameLogRegistry::RegisterScopedCategory(const char* InModuleName, FLogCategoryBase* InCategory) {
    if (!InModuleName || InModuleName[0] == '\0' || !InCategory) {
        return nullptr;
    }
    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);
    for (FModuleLogEntry& Entry : Storage.Entries) {
        if (!Entry.Matches(InModuleName)) {
            continue;
        }
        FLogCategoryBase* PreviousCategory = Entry.Category;
        if (Entry.Category == InCategory) {
            return PreviousCategory;
        }
        Entry.Category = InCategory;
        Storage.Epoch.fetch_add(1, std::memory_order_release);
        return PreviousCategory;
    }
    FModuleLogEntry& NewEntry = Storage.Entries.AddDefaulted_GetRef();
    NewEntry.Name = MakeAnsiName(InModuleName);
    NewEntry.Category = InCategory;
    Storage.Epoch.fetch_add(1, std::memory_order_release);
    return nullptr;
}

void FGameLogRegistry::UnregisterScopedCategory(const char* InModuleName, FLogCategoryBase* InCategory,
    FLogCategoryBase* InPreviousCategory) {
    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);
    for (int32 Index = 0; Index < Storage.Entries.Num(); ++Index) {
        FModuleLogEntry& Entry = Storage.Entries[Index];
        if (!Entry.Matches(InModuleName)) {
            continue;
        }

        if (Entry.Category != InCategory) {
            return;
        }

        if (InPreviousCategory != nullptr && Entry.Category == InPreviousCategory) {
            return;
        }

        if (InPreviousCategory != nullptr) {
            Entry.Category = InPreviousCategory;
        } else {
            Storage.Entries.RemoveAtSwap(Index);
        }

        Storage.Epoch.fetch_add(1, std::memory_order_release);
        return;
    }
}

} // namespace Game::Log

