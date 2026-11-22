// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file FGameLogRegistry.cpp
 * @brief 模块日志注册中心实现（常驻指针稳定槽位 + 零分配 FNV-1a 哈希表 + SIOF 免疫常驻单例）
 */

#include "Logging/FGameLogRegistry.h"

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Logging/FModuleLogSlotRecord.h"
#include "Logging/GameLogCategories.h"
#include "Misc/ScopeRWLock.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Templates/UniquePtr.h"

#include <memory>

namespace Game::Log {

namespace {

constexpr uint32 FnvOffsetBasis32 = 2166136261u;
constexpr uint32 FnvPrime32 = 16777619u;

/**
 * @brief 进程常驻注册表存储体（按需延迟构造且退出期不析构，彻底免疫 SIOF 与逆序析构崩溃）
 */
class FRegistryStorage final {
public:
    /**
     * @brief 只读 ANSI 查找视图，支持 TMap::FindByHash 零堆分配检索
     */
    struct FAnsiLookupView {
        const char* Data = nullptr;
        int32 Length = 0;
        uint32 Hash = 0;
    };

    /**
     * @brief 自持内存的区分大小写 ANSI 模块名键
     */
    struct FAnsiNameKey {
        /**
         * @brief 从查找视图深拷贝构造模块名键
         * @param View 已计算长度与哈希的源名称视图
         */
        explicit FAnsiNameKey(const FAnsiLookupView& View)
            : Hash(View.Hash) {
            Buffer.SetNumUninitialized(View.Length + 1);
            FMemory::Memcpy(Buffer.GetData(), View.Data, static_cast<size_t>(View.Length) + 1);
        }

        TArray<ANSICHAR> Buffer;
        uint32 Hash = 0;
    };

    /**
     * @brief 精确区分大小写且支持异构视图零分配查找的 TMap 键特征
     */
    struct FAnsiNameKeyFuncs
        : BaseKeyFuncs<TPair<FAnsiNameKey, TUniquePtr<FModuleLogSlotRecord>>, FAnsiNameKey, false> {
        using KeyInitType = const FAnsiNameKey&;
        using ElementInitType = const TPairInitializer<const FAnsiNameKey&, TUniquePtr<FModuleLogSlotRecord>&&>&;

        /**
         * @brief 提取元素主键引用
         * @param Element 映射表键值对元素
         * @return 键常量引用
         */
        [[nodiscard]] static FORCEINLINE KeyInitType GetSetKey(
            const TPair<FAnsiNameKey, TUniquePtr<FModuleLogSlotRecord>>& Element
        ) noexcept {
            return Element.Key;
        }

        /**
         * @brief 比较两个自持键是否字节级完全一致
         * @param A 键 A
         * @param B 键 B
         * @return 完全相同返回 true
         */
        [[nodiscard]] static FORCEINLINE bool Matches(const FAnsiNameKey& A, const FAnsiNameKey& B) noexcept {
            return A.Hash == B.Hash
                && A.Buffer.Num() == B.Buffer.Num()
                && FMemory::Memcmp(A.Buffer.GetData(), B.Buffer.GetData(), A.Buffer.Num()) == 0;
        }

        /**
         * @brief 比较自持键与零分配查找视图是否完全一致
         * @param A 映射表中的键
         * @param B 待查找的名称视图
         * @return 完全相同返回 true
         */
        [[nodiscard]] static FORCEINLINE bool Matches(const FAnsiNameKey& A, const FAnsiLookupView& B) noexcept {
            return A.Hash == B.Hash
                && A.Buffer.Num() == B.Length + 1
                && FMemory::Memcmp(A.Buffer.GetData(), B.Data, B.Length) == 0;
        }

        /**
         * @brief 读取自持键的预计算哈希值
         * @param Key 模块名键
         * @return 32 位哈希值
         */
        [[nodiscard]] static FORCEINLINE uint32 GetKeyHash(const FAnsiNameKey& Key) noexcept {
            return Key.Hash;
        }

        /**
         * @brief 读取查找视图的预计算哈希值
         * @param View 模块名查找视图
         * @return 32 位哈希值
         */
        [[nodiscard]] static FORCEINLINE uint32 GetKeyHash(const FAnsiLookupView& View) noexcept {
            return View.Hash;
        }
    };

public:
    /**
     * @brief 获取常驻进程生命周期的注册表存储单例（静态缓冲区就地构造，零裸 new 且退出不析构）
     * @return 注册表存储单例引用
     */
    [[nodiscard]] static FRegistryStorage& Get() {
        static TTypeCompatibleBytes<FRegistryStorage> RawStorage;
        static FRegistryStorage* const Instance = std::construct_at(RawStorage.GetTypedPtr());
        return *Instance;
    }

    /**
     * @brief 单趟扫描 ANSI 字符串生成含长度与 FNV-1a 哈希的只读视图（SIOF 安全）
     * @param InName 非空 ANSI 模块名
     * @return 查找视图
     */
    [[nodiscard]] static FAnsiLookupView MakeLookupView(const char* InName) noexcept {
        uint32 Hash = FnvOffsetBasis32;
        const char* Cursor = InName;
        while (*Cursor != '\0') {
            Hash ^= static_cast<uint8>(*Cursor);
            Hash *= FnvPrime32;
            ++Cursor;
        }
        return FAnsiLookupView{InName, static_cast<int32>(Cursor - InName), Hash};
    }

    /**
     * @brief 写锁下查找或创建目标模块的常驻槽位记录
     * @param View 模块名查找视图
     * @return 槽位记录指针（永不为空）
     */
    [[nodiscard]] FModuleLogSlotRecord* FindOrAddSlotLocked(const FAnsiLookupView& View) {
        if (TUniquePtr<FModuleLogSlotRecord>* Existing = Slots.FindByHash(View.Hash, View)) {
            return Existing->Get();
        }
        TUniquePtr<FModuleLogSlotRecord> Created = MakeUnique<FModuleLogSlotRecord>(&FGameLogRegistry::GetFallbackCategory());
        FModuleLogSlotRecord* RawPtr = Created.Get();
        Slots.Emplace(FAnsiNameKey(View), MoveTemp(Created));
        return RawPtr;
    }

    /**
     * @brief 只读查找已存在的模块槽位记录（调用方需已持有读锁或写锁）
     * @param View 模块名查找视图
     * @return 找到返回记录指针，否则返回 nullptr
     */
    [[nodiscard]] FModuleLogSlotRecord* FindSlotLocked(const FAnsiLookupView& View) {
        TUniquePtr<FModuleLogSlotRecord>* Existing = Slots.FindByHash(View.Hash, View);
        return (Existing != nullptr) ? Existing->Get() : nullptr;
    }

public:
    FRWLock Lock;
    TMap<FAnsiNameKey, TUniquePtr<FModuleLogSlotRecord>, FDefaultSetAllocator, FAnsiNameKeyFuncs> Slots;
};

} // namespace

void FGameLogRegistry::RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory) {
    FLogCategoryBase* UnusedPrevious = nullptr;
    (void)RegisterScopedCategory(InModuleName, InCategory, UnusedPrevious);
}

void FGameLogRegistry::UnregisterCategory(const char* InModuleName) {
    if (InModuleName == nullptr || InModuleName[0] == '\0') {
        return;
    }

    const FRegistryStorage::FAnsiLookupView View = FRegistryStorage::MakeLookupView(InModuleName);
    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);

    FModuleLogSlotRecord* Record = Storage.FindSlotLocked(View);
    if (Record == nullptr || !Record->bRegistered) {
        return;
    }

    Record->bRegistered = false;
    Record->Category.store(&GetFallbackCategory(), std::memory_order_release);
}

FLogCategoryBase& FGameLogRegistry::GetCategory(const char* InModuleName) {
    if (InModuleName == nullptr || InModuleName[0] == '\0') {
        return GetFallbackCategory();
    }

    const FRegistryStorage::FAnsiLookupView View = FRegistryStorage::MakeLookupView(InModuleName);
    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock ReadLock(Storage.Lock, SLT_ReadOnly);

    const FModuleLogSlotRecord* Record = Storage.FindSlotLocked(View);
    if (Record == nullptr) {
        return GetFallbackCategory();
    }
    return *Record->Category.load(std::memory_order_acquire);
}

FLogCategoryBase& FGameLogRegistry::GetFallbackCategory() {
#if NO_LOGGING
    static FLogCategoryBase NoLoggingFallback(TEXT("LogGame"), ELogVerbosity::Log, ELogVerbosity::All);
    return NoLoggingFallback;
#else
    return LogGame;
#endif
}

const FLogCategorySlot* FGameLogRegistry::ResolveCategorySlot(const char* InModuleName) {
    if (InModuleName == nullptr || InModuleName[0] == '\0') {
        static const FLogCategorySlot FallbackSlot(&GetFallbackCategory());
        return &FallbackSlot;
    }

    const FRegistryStorage::FAnsiLookupView View = FRegistryStorage::MakeLookupView(InModuleName);
    FRegistryStorage& Storage = FRegistryStorage::Get();

    {
        FRWScopeLock ReadLock(Storage.Lock, SLT_ReadOnly);
        if (const FModuleLogSlotRecord* Existing = Storage.FindSlotLocked(View)) {
            return &Existing->Category;
        }
    }

    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);
    const FModuleLogSlotRecord* Record = Storage.FindOrAddSlotLocked(View);
    return &Record->Category;
}

FModuleLogSlotRecord* FGameLogRegistry::RegisterScopedCategory(
    const char* InModuleName,
    FLogCategoryBase* InCategory,
    FLogCategoryBase*& OutPreviousCategory
) {
    OutPreviousCategory = nullptr;
    if (InModuleName == nullptr || InModuleName[0] == '\0' || InCategory == nullptr) {
        return nullptr;
    }

    const FRegistryStorage::FAnsiLookupView View = FRegistryStorage::MakeLookupView(InModuleName);
    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);

    FModuleLogSlotRecord* Record = Storage.FindOrAddSlotLocked(View);
    FLogCategoryBase* CurrentCategory = Record->Category.load(std::memory_order_relaxed);
    if (Record->bRegistered) {
        OutPreviousCategory = CurrentCategory;
        if (CurrentCategory == InCategory) {
            return Record;
        }
    }

    Record->bRegistered = true;
    Record->Category.store(InCategory, std::memory_order_release);
    return Record;
}

void FGameLogRegistry::UnregisterScopedCategory(
    FModuleLogSlotRecord* InSlotRecord,
    FLogCategoryBase* InCategory,
    FLogCategoryBase* InPreviousCategory
) {
    if (InSlotRecord == nullptr) {
        return;
    }

    FRegistryStorage& Storage = FRegistryStorage::Get();
    FRWScopeLock WriteLock(Storage.Lock, SLT_Write);

    if (!InSlotRecord->bRegistered) {
        return;
    }

    FLogCategoryBase* CurrentCategory = InSlotRecord->Category.load(std::memory_order_relaxed);
    if (CurrentCategory != InCategory) {
        return;
    }

    if (InPreviousCategory != nullptr && CurrentCategory == InPreviousCategory) {
        return;
    }

    if (InPreviousCategory != nullptr) {
        InSlotRecord->Category.store(InPreviousCategory, std::memory_order_release);
        return;
    }

    InSlotRecord->bRegistered = false;
    InSlotRecord->Category.store(&GetFallbackCategory(), std::memory_order_release);
}

} // namespace Game::Log
