/**
 * @file FConfigRegistry.cpp
 * @brief FConfigRegistry 非模板实现（模板实现见 FConfigRegistry.h）
 */

#include "Config/FConfigRegistry.h"

#include <type_traits>

#include "Config/GameCompileSwitches.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
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
     * @brief 在监听锁内抓取 COW 快照并移动变体追加至全局 FIFO
     * @param InValue 本次提交的值变体右值
     * @return 当前线程当选为 drainer 返回 true
     */
    [[nodiscard]] bool Enqueue(FConfigVariant&& InValue) const;

    /**
     * @brief 按订阅 ID 移除监听
     * @param SubscriptionId 订阅 ID
     */
    void RemoveListener(uint64 SubscriptionId) override;

private:
    mutable FRWLock WatcherLock;
    TSharedPtr<const TArray<FWatcherEntry>> Entries;
    uint64 NextId = 0;
};

/**
 * @brief 追加监听并返回唯一订阅 ID（写时复制更新回调表）
 * @param InListener 类型擦除回调
 * @return 订阅 ID
 */
uint64 FConfigRegistry::FWatcherState::AddListener(FCallback InListener) {
    if (!InListener) {
        return 0;
    }
    FWriteScopeLock WriteLock(WatcherLock);
    TArray<FWatcherEntry> Updated;
    if (Entries.IsValid()) {
        Updated.Reserve(Entries->Num() + 1);
        Updated.Append(*Entries);
    } else {
        Updated.Reserve(1);
    }
    const uint64 SubscriptionId = ++NextId;
    Updated.Add(FWatcherEntry{SubscriptionId, MoveTemp(InListener)});
    Entries = MakeShared<const TArray<FWatcherEntry>>(MoveTemp(Updated));
    return SubscriptionId;
}

/**
 * @brief 在监听锁内抓取 COW 回调表指针并移动变体入队全局 FIFO
 * @param InValue 本次提交的值变体右值
 * @return 当前线程当选为 drainer 返回 true
 */
bool FConfigRegistry::FWatcherState::Enqueue(FConfigVariant&& InValue) const {
    TSharedPtr<const TArray<FWatcherEntry>> Snapshot;
    {
        FReadScopeLock ReadLock(WatcherLock);
        Snapshot = Entries;
    }
    if (!Snapshot.IsValid() || Snapshot->IsEmpty()) {
        return false;
    }
    return FNotificationDispatcher::Get().Enqueue(
        [Snapshot = MoveTemp(Snapshot), BroadcastValue = MoveTemp(InValue)]() {
            for (const FWatcherEntry& Entry : *Snapshot) {
                Entry.Callback(BroadcastValue);
            }
        });
}

/**
 * @brief 按订阅 ID 移除监听（写时复制更新回调表）
 * @param SubscriptionId 订阅 ID
 */
void FConfigRegistry::FWatcherState::RemoveListener(uint64 SubscriptionId) {
    FWriteScopeLock WriteLock(WatcherLock);
    if (!Entries.IsValid()) {
        return;
    }
    TArray<FWatcherEntry> Updated = *Entries;
    const int32 RemovedCount = Updated.RemoveAll([SubscriptionId](const FWatcherEntry& Entry) {
        return Entry.SubscriptionId == SubscriptionId;
    });
    if (RemovedCount > 0) {
        Entries = MakeShared<const TArray<FWatcherEntry>>(MoveTemp(Updated));
    }
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
 * @brief 查询指定配置键在当前构建配置下是否属于只读保护键
 * @param Name 配置键
 * @return 属于只读键返回 true
 */
bool FConfigRegistry::IsReadOnly(FName Name) const {
    FReadScopeLock ReadLock(RwLock);
    if (const FEntry* Entry = Entries.Find(Name)) {
        return Entry->bReadOnly;
    }
    return IsReadOnlyKeyName(Name);
}

/**
 * @brief 按类型擦除变体获取已注册配置值快照（未注册时返回空可选值）
 * @param Name 配置键
 * @return 变体可选对象
 */
TOptional<FConfigRegistry::FConfigVariant> FConfigRegistry::GetRawConfigOptional(FName Name) const {
    FReadScopeLock ReadLock(RwLock);
    if (const FEntry* Entry = Entries.Find(Name)) {
        return TOptional<FConfigVariant>(Entry->Value);
    }
    return TOptional<FConfigVariant>();
}

/**
 * @brief 获取全局版本号（每次注册/覆盖自增）
 * @return 当前版本号
 */
uint32 FConfigRegistry::GetVersion() const noexcept {
    FReadScopeLock ReadLock(RwLock);
    return GlobalVersion;
}

namespace {

static FRWLock GWorldRegistryLock;
static TMap<TWeakObjectPtr<const UWorld>, TSharedPtr<FConfigRegistry>> GWorldRegistries;
static bool bHookedWorldDestroy = false;

static TSharedPtr<FConfigRegistry> GetGlobalShared() {
    static TSharedPtr<FConfigRegistry> GlobalInstance = MakeShared<FConfigRegistry>();
    return GlobalInstance;
}

}  // namespace

/**
 * @brief 从另一个配置注册表继承已注册条目（用于多 World 实例继承全局默认配置）
 * @param Other 源注册表引用
 */
void FConfigRegistry::InheritFrom(const FConfigRegistry& Other) {
    FReadScopeLock ReadLock(Other.RwLock);
    FWriteScopeLock WriteLock(RwLock);
    Entries = Other.Entries;
    GlobalVersion = Other.GlobalVersion;
}

/**
 * @brief 获取配置注册表的共享指针（推荐跨帧或持久持有使用，完全杜绝 World 销毁导致的悬垂崩溃）
 * @param WorldContextObject 关联的世界上下文对象（为空时返回全局根注册表）
 * @return 对应的配置注册表强引用共享指针
 */
TSharedPtr<FConfigRegistry> FConfigRegistry::GetShared(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr) {
        return GetGlobalShared();
    }

    const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (World == nullptr) {
        return GetGlobalShared();
    }

    {
        FReadScopeLock ReadLock(GWorldRegistryLock);
        if (const TSharedPtr<FConfigRegistry>* Found = GWorldRegistries.Find(World)) {
            if (Found->IsValid()) {
                return *Found;
            }
        }
    }

    FWriteScopeLock WriteLock(GWorldRegistryLock);
    if (const TSharedPtr<FConfigRegistry>* Found = GWorldRegistries.Find(World)) {
        if (Found->IsValid()) {
            return *Found;
        }
    }

    if (!bHookedWorldDestroy) {
        FWorldDelegates::OnPreWorldFinishDestroy.AddLambda([](UWorld* DyingWorld) {
            FWriteScopeLock CleanupLock(GWorldRegistryLock);
            GWorldRegistries.Remove(DyingWorld);
        });
        bHookedWorldDestroy = true;
    }

    TSharedPtr<FConfigRegistry> NewRegistry = MakeShared<FConfigRegistry>();
    NewRegistry->InheritFrom(*GetGlobalShared());
    GWorldRegistries.Add(World, NewRegistry);
    return NewRegistry;
}

/**
 * @brief 获取配置注册表（支持 World 上下文路由与多实例隔离）
 * @param WorldContextObject 关联的世界上下文对象（为空时返回全局根注册表）
 * @return 对应的配置注册表引用
 */
FConfigRegistry& FConfigRegistry::Get(const UObject* WorldContextObject) {
    TSharedPtr<FConfigRegistry> RegistryPtr = GetShared(WorldContextObject);
    check(RegistryPtr.IsValid());
    return *RegistryPtr;
}

/**
 * @brief 清空所有已缓存的 World 隔离注册表（供自动化测试清理）
 */
void FConfigRegistry::ClearWorldRegistriesForTesting() {
    FWriteScopeLock WriteLock(GWorldRegistryLock);
    GWorldRegistries.Empty();
}

/**
 * @brief 注册默认值（仅未注册时写入）
 * @param Name 配置键
 * @param DefaultValue 默认值变体
 */
void FConfigRegistry::ApplyRegister(FName Name, FConfigVariant&& DefaultValue) {
    TSharedPtr<FWatcherState> WatcherToNotify;
    TOptional<FConfigVariant> BroadcastValue;
    {
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
        if (TSharedPtr<FWatcherState>* Found = Watchers.Find(Name)) {
            if (Found->IsValid()) {
                WatcherToNotify = *Found;
                BroadcastValue.Emplace(Entry.Value);
            }
        }
    }
    if (WatcherToNotify.IsValid() && BroadcastValue.IsSet()) {
        if (WatcherToNotify->Enqueue(MoveTemp(*BroadcastValue))) {
            FNotificationDispatcher::Get().Drain();
        }
    }
}

/**
 * @brief 覆盖写入并广播订阅（只读键拒绝）
 * @param Name 配置键
 * @param NewValue 新值变体
 * @return 写入结果码
 */
ESetConfigResult FConfigRegistry::ApplySet(FName Name, FConfigVariant&& NewValue) {
    FString LoggedTypeName;
    TSharedPtr<FWatcherState> WatcherToNotify;
    TOptional<FConfigVariant> BroadcastValue;
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
            if constexpr (Game::Config::bEnableLog) {
                LoggedTypeName = VariantTypeName(Entry->Value);
            }
            if (TSharedPtr<FWatcherState>* Found = Watchers.Find(Name)) {
                if (Found->IsValid()) {
                    WatcherToNotify = *Found;
                    BroadcastValue.Emplace(Entry->Value);
                }
            }
        }
    }
    if (bRejected) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameConfig, Warning, TEXT("【配置注册表】设置配置失败：只读配置键 [%s] 不可覆盖。"), *Name.ToString());
        }
        return ESetConfigResult::RejectedReadOnly;
    }
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameConfig, Log, TEXT("【配置注册表】更新配置项: 键=[%s], 类型=[%s]"),
            *Name.ToString(), *LoggedTypeName);
    }
    if (WatcherToNotify.IsValid() && BroadcastValue.IsSet()) {
        if (WatcherToNotify->Enqueue(MoveTemp(*BroadcastValue))) {
            FNotificationDispatcher::Get().Drain();
        }
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
    uint64 SubscriptionId = 0;
    {
        FWriteScopeLock WriteLock(RwLock);
        TSharedPtr<FWatcherState>& Slot = Watchers.FindOrAdd(Name);
        if (!Slot.IsValid()) {
            Slot = MakeShared<FWatcherState>();
        }
        State = Slot;
        SubscriptionId = State->AddListener(MoveTemp(InListener));
    }
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameConfig, Verbose, TEXT("【配置注册表】添加配置监听: 键=[%s], 订阅 ID=%llu"), *Name.ToString(), SubscriptionId);
    }
    const TSharedRef<FObserverSubscription> Subscription = MakeShared<FObserverSubscription>();
    if (SubscriptionId != 0) {
        Subscription->Initialize(State.ToSharedRef(), SubscriptionId);
    }
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
