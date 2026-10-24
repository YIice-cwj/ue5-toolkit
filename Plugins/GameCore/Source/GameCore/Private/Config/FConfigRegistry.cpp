/**
 * @file FConfigRegistry.cpp
 * @brief FConfigRegistry 非模板实现（模板实现见 FConfigRegistry.h）
 */

#include "Config/FConfigRegistry.h"

#include <type_traits>

#include "Config/GameCompileSwitches.h"

namespace {

/**
 * @brief 判定配置键是否属于只读域
 * @param Name 配置键
 * @return 只读返回 true
 *
 * 实现思路：Env.* 为构建态环境信息恒只读；未开启对应编译开关时 Dev.* 与 Feature.GM 恒只读，防调试后门。
 */
bool IsReadOnlyKeyName(const FName& Name) {
    const FString KeyText = Name.ToString();
    if (KeyText.StartsWith(TEXT("Env."))) {
        return true;
    }
    if constexpr (!Game::Config::bEnableDebugDraw) {
        if (KeyText.StartsWith(TEXT("Dev."))) {
            return true;
        }
    }
    if constexpr (!Game::Config::bEnableGM) {
        if (KeyText == TEXT("Feature.GM")) {
            return true;
        }
    }
    return false;
}

}  // namespace

/**
 * @brief 单键订阅状态：持类型擦除回调条目，锁内拷贝快照、锁外派发
 *
 * 实现思路：写路径（订阅/退订）在写锁内完成数组增删；广播路径仅在读锁内拷贝条目快照，
 * 出锁后逐个回调——持锁严禁调用任何用户回调（FRWLock 不可重入）。
 */
class FConfigRegistry::FWatcherState : public FObserverStateBase {
public:
    using FCallback = TFunction<void(const FConfigVariant&)>;

private:
    /** 订阅条目：唯一 ID + 类型擦除回调（TFunction 无法按值比较，退订只能按 ID 匹配） */
    struct FWatcherEntry {
        uint64 SubscriptionId = 0;
        FCallback Callback;
    };

public:
    FWatcherState() = default;

    /**
     * @brief 追加监听并返回唯一订阅 ID
     * @param InListener 类型擦除回调
     * @return 订阅 ID
     */
    uint64 AddListener(FCallback InListener);

    /**
     * @brief 广播新值（快照遍历，回调外派）
     * @param InValue 新值变体
     */
    void Broadcast(const FConfigVariant& InValue) const;

    /**
     * @brief 按订阅 ID 移除监听
     * @param SubscriptionId 订阅 ID
     */
    void RemoveListener(uint64 SubscriptionId) override;

private:
    mutable FRWLock WatcherLock;
    TArray<FWatcherEntry> Entries;
    uint64 NextId = 0;
};

uint64 FConfigRegistry::FWatcherState::AddListener(FCallback InListener) {
    if (!InListener) {
        return 0;
    }
    FWriteScopeLock WriteLock(WatcherLock);
    const uint64 SubscriptionId = ++NextId;
    Entries.Add(FWatcherEntry{SubscriptionId, MoveTemp(InListener)});
    return SubscriptionId;
}

void FConfigRegistry::FWatcherState::Broadcast(const FConfigVariant& InValue) const {
    TArray<FWatcherEntry> Snapshot;
    {
        FReadScopeLock ReadLock(WatcherLock);
        Snapshot = Entries;
    }
    for (const FWatcherEntry& Entry : Snapshot) {
        Entry.Callback(InValue);
    }
}

void FConfigRegistry::FWatcherState::RemoveListener(uint64 SubscriptionId) {
    FWriteScopeLock WriteLock(WatcherLock);
    Entries.RemoveAll([SubscriptionId](const FWatcherEntry& Entry) {
        return Entry.SubscriptionId == SubscriptionId;
    });
}

bool FConfigRegistry::IsRegistered(FName Name) const {
    FReadScopeLock ReadLock(RwLock);
    return Entries.Contains(Name);
}

uint32 FConfigRegistry::GetVersion() const noexcept {
    FReadScopeLock ReadLock(RwLock);
    return GlobalVersion;
}

void FConfigRegistry::ApplyRegister(FName Name, FConfigVariant&& DefaultValue) {
    FWriteScopeLock WriteLock(RwLock);
    if (Entries.Contains(Name)) {
        return;
    }
    FEntry& Entry = Entries.Emplace(Name);
    Entry.Value = MoveTemp(DefaultValue);
    Entry.bReadOnly = IsReadOnlyKeyName(Name);
    ++Entry.Version;
    ++GlobalVersion;
}

ESetConfigResult FConfigRegistry::ApplySet(FName Name, FConfigVariant&& NewValue) {
    TSharedPtr<FWatcherState> Watcher;
    FConfigVariant BroadcastValue;
    bool bRejected = false;
    {
        FWriteScopeLock WriteLock(RwLock);
        FEntry* Entry = Entries.Find(Name);
        if ((Entry != nullptr && Entry->bReadOnly) || (Entry == nullptr && IsReadOnlyKeyName(Name))) {
            bRejected = true;
        } else {
            if (Entry == nullptr) {
                Entry = &Entries.Emplace(Name);
            }
            Entry->Value = MoveTemp(NewValue);
            ++Entry->Version;
            ++GlobalVersion;
            BroadcastValue = Entry->Value;
            if (TSharedPtr<FWatcherState>* Found = Watchers.Find(Name)) {
                Watcher = *Found;
            }
        }
    }
    if (bRejected) {
        ensureMsgf(false, TEXT("【配置注册表】SetConfig 拒绝：只读配置键 [%s] 不可覆盖！"), *Name.ToString());
        return ESetConfigResult::RejectedReadOnly;
    }
    if (Watcher.IsValid()) {
        Watcher->Broadcast(BroadcastValue);
    }
    return ESetConfigResult::Applied;
}

TSharedRef<FObserverSubscription> FConfigRegistry::AddWatcher(FName Name, TFunction<void(const FConfigVariant&)> InListener) {
    TSharedPtr<FWatcherState> State;
    {
        FWriteScopeLock WriteLock(RwLock);
        TSharedPtr<FWatcherState>& Slot = Watchers.FindOrAdd(Name);
        if (!Slot.IsValid()) {
            Slot = MakeShared<FWatcherState>();
        }
        State = Slot;
    }
    const uint64 SubscriptionId = State->AddListener(MoveTemp(InListener));
    const TSharedRef<FObserverSubscription> Subscription = MakeShared<FObserverSubscription>();
    Subscription->Initialize(State.ToSharedRef(), SubscriptionId);
    return Subscription;
}

FConfigRegistry& FConfigRegistry::Get() {
    static FConfigRegistry Instance;
    return Instance;
}

FString FConfigRegistry::VariantTypeName(const FConfigVariant& InVariant) {
    FString TypeName = TEXT("未知类型");
    Visit([&TypeName](const auto& Stored) {
        using FStoredType = std::decay_t<decltype(Stored)>;
        if constexpr (std::is_same_v<FStoredType, bool>) {
            TypeName = TEXT("bool");
        } else if constexpr (std::is_same_v<FStoredType, int32>) {
            TypeName = TEXT("int32");
        } else if constexpr (std::is_same_v<FStoredType, int64>) {
            TypeName = TEXT("int64");
        } else if constexpr (std::is_same_v<FStoredType, float>) {
            TypeName = TEXT("float");
        } else if constexpr (std::is_same_v<FStoredType, double>) {
            TypeName = TEXT("double");
        } else if constexpr (std::is_same_v<FStoredType, FString>) {
            TypeName = TEXT("FString");
        } else if constexpr (std::is_same_v<FStoredType, FName>) {
            TypeName = TEXT("FName");
        } else if constexpr (std::is_same_v<FStoredType, FVector>) {
            TypeName = TEXT("FVector");
        } else if constexpr (std::is_same_v<FStoredType, FRotator>) {
            TypeName = TEXT("FRotator");
        } else if constexpr (std::is_same_v<FStoredType, FGameplayTag>) {
            TypeName = TEXT("FGameplayTag");
        }
    }, InVariant);
    return TypeName;
}
