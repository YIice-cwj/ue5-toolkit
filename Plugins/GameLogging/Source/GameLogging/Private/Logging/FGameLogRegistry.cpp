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
     * @brief 获取延迟初始化的进程级注册表存储
     * @return 注册表存储单例引用
     */
    static FRegistryStorage& Get() {
        static FRegistryStorage Instance;
        return Instance;
    }

public:
    FRWLock Lock;
    TArray<FModuleLogEntry> Entries;
    std::atomic<int32> Epoch{0};
};

/**
 * @brief 拷贝 ANSI 模块名（含结尾符），调用方临时缓冲区亦可安全注册
 * @param InName 源名称
 * @return 含结尾符的模块名字符数组
 */
[[nodiscard]] static TArray<ANSICHAR> MakeAnsiName(const char* InName) {
    const int32 Length = FCStringAnsi::Strlen(InName);
    TArray<ANSICHAR> Result;
    Result.SetNumUninitialized(Length + 1);
    FCStringAnsi::Strncpy(Result.GetData(), InName, Length + 1);
    return Result;
}

} // namespace

void FGameLogRegistry::RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory) {
    if (!InModuleName || InModuleName[0] == '\0' || !InCategory) {
        return;
    }
    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);
    for (FModuleLogEntry& Entry : Storage.Entries) {
        if (Entry.Matches(InModuleName)) {
            Entry.Category = InCategory;
            Storage.Epoch.fetch_add(1, std::memory_order_release);
            return;
        }
    }
    FModuleLogEntry& NewEntry = Storage.Entries.AddDefaulted_GetRef();
    NewEntry.Name = MakeAnsiName(InModuleName);
    NewEntry.Category = InCategory;
    Storage.Epoch.fetch_add(1, std::memory_order_release);
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
    if (InModuleName && InModuleName[0] != '\0') {
        FRegistryStorage& Storage = FRegistryStorage::Get();
        FRWScopeLock ReadLock(Storage.Lock, SLT_ReadOnly);
        for (const FModuleLogEntry& Entry : Storage.Entries) {
            if (Entry.Matches(InModuleName)) {
                return *Entry.Category;
            }
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

FLogCategoryBase* FGameLogRegistry::RegisterScopedCategory(const char* InModuleName, FLogCategoryBase* InCategory) {
    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);
    for (FModuleLogEntry& Entry : Storage.Entries) {
        if (Entry.Matches(InModuleName)) {
            FLogCategoryBase* PreviousCategory = Entry.Category;
            Entry.Category = InCategory;
            Storage.Epoch.fetch_add(1, std::memory_order_release);
            return PreviousCategory;
        }
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
        if (Entry.Matches(InModuleName)) {
            if (Entry.Category != InCategory) {
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
}

} // namespace Game::Log

