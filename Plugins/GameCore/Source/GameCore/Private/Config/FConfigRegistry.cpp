/**
 * @file FConfigRegistry.cpp
 * @brief FConfigRegistry 非模板实现（模板实现见 FConfigRegistry.h）
 */

#include "Config/FConfigRegistry.h"

#include <type_traits>

#include "Config/GameCompileSwitches.h"
#include "Listener/FNotificationDispatcher.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameConfig, Log, All);

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
 * @brief 单键订阅状态：持类型擦除回调条目，锁内拷贝快照并入队、锁外统一派发
 *
 * 实现思路：写路径（订阅/退订）在写锁内完成数组增删；通知路径在读锁内拷贝条目快照并追加全局 FIFO，
 * drainer 释放注册表与监听锁后逐个回调，严禁持锁调用任何用户回调。
 */
class FConfigRegistry::FWatcherState : public FObserverStateBase {
public:
    using FCallback = TFunction<void(const FConfigVariant&)>;

private:
    /**
     * @brief 订阅条目结构（唯一 ID + 类型擦除回调，退订按 ID 匹配）
     */
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
     * @brief 在监听锁内抓取快照并追加全局 FIFO
     * @param InValue 本次提交的值变体
     * @return 当前线程当选为 drainer 返回 true
     */
    [[nodiscard]] bool Enqueue(const FConfigVariant& InValue) const;

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

bool FConfigRegistry::FWatcherState::Enqueue(const FConfigVariant& InValue) const {
    TArray<FWatcherEntry> Snapshot;
    {
        FReadScopeLock ReadLock(WatcherLock);
        Snapshot = Entries;
    }
    if (Snapshot.IsEmpty()) {
        return false;
    }
    return FNotificationDispatcher::Get().Enqueue(
        [Snapshot = MoveTemp(Snapshot), BroadcastValue = InValue]() {
            for (const FWatcherEntry& Entry : Snapshot) {
                Entry.Callback(BroadcastValue);
            }
        });
}

void FConfigRegistry::FWatcherState::RemoveListener(uint64 SubscriptionId) {
    FWriteScopeLock WriteLock(WatcherLock);
    Entries.RemoveAll([SubscriptionId](const FWatcherEntry& Entry) {
        return Entry.SubscriptionId == SubscriptionId;
    });
}

/**
 * @brief 查询配置键是否已注册
 * @param Name 配置键
 * @return 已注册返回 true
 */
bool FConfigRegistry::IsRegistered(FName Name) const {
    FReadScopeLock ReadLock(RwLock);
    return Entries.Contains(Name);
}

/**
 * @brief 获取全局版本号（每次注册/覆盖自增）
 * @return 当前版本号
 */
uint32 FConfigRegistry::GetVersion() const noexcept {
    FReadScopeLock ReadLock(RwLock);
    return GlobalVersion;
}

/**
 * @brief 获取全局单例
 * @return 单例引用
 */
FConfigRegistry& FConfigRegistry::Get() {
    static FConfigRegistry Instance;
    return Instance;
}

/**
 * @brief 注册默认值（仅未注册时写入）
 * @param Name 配置键
 * @param DefaultValue 默认值变体
 */
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
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameConfig, Log, TEXT("【配置注册表】注册配置项: 键=[%s], 类型=[%s], 只读=%s"),
            *Name.ToString(), *VariantTypeName(Entry.Value), Entry.bReadOnly ? TEXT("True") : TEXT("False"));
    }
}

/**
 * @brief 覆盖写入并广播订阅（只读键拒绝）
 * @param Name 配置键
 * @param NewValue 新值变体
 * @return 写入结果码
 */
ESetConfigResult FConfigRegistry::ApplySet(FName Name, FConfigVariant&& NewValue) {
    FConfigVariant BroadcastValue;
    bool bRejected = false;
    bool bShouldDrain = false;
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
                bShouldDrain = (*Found)->Enqueue(BroadcastValue);
            }
        }
    }
    if (bRejected) {
        ensureAlwaysMsgf(false, TEXT("【配置注册表】SetConfig 拒绝：只读配置键 [%s] 不可覆盖！"), *Name.ToString());
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameConfig, Warning, TEXT("【配置注册表】设置配置失败：只读配置键 [%s] 不可覆盖。"), *Name.ToString());
        }
        return ESetConfigResult::RejectedReadOnly;
    }
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameConfig, Log, TEXT("【配置注册表】更新配置项: 键=[%s], 类型=[%s]"),
            *Name.ToString(), *VariantTypeName(BroadcastValue));
    }
    if (bShouldDrain) {
        FNotificationDispatcher::Get().Drain();
    }
    return ESetConfigResult::Applied;
}

/**
 * @brief 注册键订阅（懒创建订阅状态）
 * @param Name 配置键
 * @param InListener 类型擦除回调
 * @return RAII 订阅守卫
 */
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
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameConfig, Verbose, TEXT("【配置注册表】添加配置监听: 键=[%s], 订阅 ID=%llu"), *Name.ToString(), SubscriptionId);
    }
    const TSharedRef<FObserverSubscription> Subscription = MakeShared<FObserverSubscription>();
    Subscription->Initialize(State.ToSharedRef(), SubscriptionId);
    return Subscription;
}

/**
 * @brief 取变体当前承载的类型名（诊断用）
 * @param InVariant 源变体
 * @return 类型名文案
 */
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
