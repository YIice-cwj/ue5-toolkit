/**
 * @file GameplayMessageSubsystem.cpp
 * @brief 全局解耦消息总线子系统薄门面实现（编排 FMessageListenerRegistry 与 FMessageDispatchQueue）
 */

#include "Message/GameplayMessageSubsystem.h"

#include "Config/GameCompileSwitches.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameMessage.h"
#include "Message/FMessageDispatchQueue.h"
#include "Message/FMessageListenerRegistry.h"

/**
 * @brief UGameplayMessageSubsystem 私有状态聚合体（Pimpl，保持 CDO 零堆分配）
 */
struct UGameplayMessageSubsystem::FImpl {
    /**
     * @brief 构造聚合内部状态
     * @param InOwner 所属消息子系统（主线程构造）
     */
    explicit FImpl(UGameplayMessageSubsystem* InOwner)
        : DispatchQueue(InOwner) {
    }

    Game::Message::Private::FMessageListenerRegistry Registry;
    Game::Message::Private::FMessageDispatchQueue DispatchQueue;
};

/**
 * @brief 默认构造函数（保持 Pimpl 为空，实现 CDO 零堆分配）
 */
UGameplayMessageSubsystem::UGameplayMessageSubsystem() = default;

/**
 * @brief 析构函数（释放内部 Pimpl 资源）
 */
UGameplayMessageSubsystem::~UGameplayMessageSubsystem() = default;

/**
 * @brief 获取子系统单例引用（断言有效）
 * @param WorldContextObject 关联的世界上下文对象（支持 UWorld、UGameInstance、UGameInstanceSubsystem 及场景对象）
 * @return 子系统引用
 */
UGameplayMessageSubsystem& UGameplayMessageSubsystem::Get(const UObject* WorldContextObject) {
    UGameplayMessageSubsystem* Subsystem = GetPtr(WorldContextObject);
    checkf(
        Subsystem != nullptr,
        TEXT("UGameplayMessageSubsystem::Get failed: WorldContextObject '%s' has no valid GameInstance."),
        *GetNameSafe(WorldContextObject)
    );
    return *Subsystem;
}

/**
 * @brief 安全获取子系统指针（可空）
 * @param WorldContextObject 关联的世界上下文对象（支持 UWorld、UGameInstance、UGameInstanceSubsystem 及场景对象）
 * @return 子系统指针，未就绪时返回 nullptr
 */
UGameplayMessageSubsystem* UGameplayMessageSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr) {
        return nullptr;
    }
    if (const UGameInstance* GameInstance = Cast<UGameInstance>(WorldContextObject)) {
        return GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    }
    if (const UGameInstanceSubsystem* OtherSubsystem = Cast<UGameInstanceSubsystem>(WorldContextObject)) {
        if (const UGameInstance* GameInstance = OtherSubsystem->GetGameInstance()) {
            return GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
        }
    }
    if (GEngine != nullptr) {
        if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
            if (const UGameInstance* GameInstance = World->GetGameInstance()) {
                return GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
            }
        }
    }
    return nullptr;
}

/**
 * @brief 子系统初始化生命周期回调：初始化内部注册表与跨线程派发队列
 * @param Collection 子系统集合
 */
void UGameplayMessageSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    FImpl& State = EnsureImpl();
    State.Registry.BeginEpoch();
    State.DispatchQueue.ResetForInitialize();
}

/**
 * @brief 子系统销毁生命周期回调：关闭跨线程入队通道、丢弃残留任务、失活并清空所有监听器且作废旧句柄纪元
 */
void UGameplayMessageSubsystem::Deinitialize() {
    if (Impl.IsValid()) {
        Impl->DispatchQueue.Shutdown();
        const int32 ClearedChannels = Impl->Registry.ClearAll();
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameMessage, Log, TEXT("【消息子系统】子系统注销，清空 %d 个通道的监听器。"), ClearedChannels);
        }
    }
    Super::Deinitialize();
}

/**
 * @brief 检查向目标通道广播时是否存在可命中的活跃监听器（与 BroadcastMessage 路由完全一致，自动覆盖祖先通道 PartialMatch）
 * @param Channel 目标广播通道
 * @return 存在可命中监听器返回 true
 */
bool UGameplayMessageSubsystem::HasListener(FGameplayTag Channel) const {
    check(IsInGameThread());
    if (!Impl.IsValid()) {
        return false;
    }
    return Impl->Registry.HasListener(Channel);
}

/**
 * @brief 在主线程立即排空并派发所有跨线程待处理消息队列
 * @return 本次排空实际派发的消息数量
 */
int32 UGameplayMessageSubsystem::FlushPendingMessages() {
    check(IsInGameThread());
    if (!Impl.IsValid()) {
        return 0;
    }
    return Impl->DispatchQueue.FlushPending();
}

/**
 * @brief 查询当前已注册的活跃监听器总数（O(1)，供测试或诊断使用）
 * @return 监听器数量
 */
int32 UGameplayMessageSubsystem::GetListenerCount() const {
    check(IsInGameThread());
    if (!Impl.IsValid()) {
        return 0;
    }
    return Impl->Registry.GetListenerCount();
}

/**
 * @brief 主线程惰性获取或创建内部 Pimpl 实例
 * @return 内部状态引用
 */
UGameplayMessageSubsystem::FImpl& UGameplayMessageSubsystem::EnsureImpl() {
    check(IsInGameThread());
    if (!Impl.IsValid()) {
        Impl = MakeShared<FImpl>(this);
    }
    return *Impl;
}

/**
 * @brief 内部注册监听器实现函数
 * @param Channel 注册目标通道
 * @param StructType 期望匹配的反射结构体元数据指针（为空代表通配）
 * @param Callback 底层类型擦除回调闭包
 * @param MatchRule 匹配模式
 * @return RAII 订阅句柄
 */
FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListenerInternal(
    FGameplayTag Channel,
    const UScriptStruct* StructType,
    FRawMessageCallback Callback,
    EGameplayMessageMatch MatchRule
) {
    check(IsInGameThread());
    if (!Channel.IsValid() || !Callback) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameMessage, Warning, TEXT("【消息子系统】注册监听器失败：通道标签无效或回调为空。"));
        }
        return FGameplayMessageListenerHandle();
    }
    if (Impl.IsValid() && Impl->DispatchQueue.IsShuttingDown()) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameMessage, Warning, TEXT("【消息子系统】注册监听器失败：子系统已关闭，请等待重新初始化。"));
        }
        return FGameplayMessageListenerHandle();
    }

    const Game::Message::Private::FMessageListenerRegistry::FRegisterReceipt Receipt =
        EnsureImpl().Registry.Register(Channel, StructType, MoveTemp(Callback), MatchRule);
    return FGameplayMessageListenerHandle(this, Channel, Receipt.ListenerId, Receipt.ActiveEpochWeak);
}

/**
 * @brief 内部按通道与唯一 ID 注销监听器（仅供 FGameplayMessageListenerHandle 友元调用）
 * @param Channel 目标通道
 * @param ListenerId 监听器唯一 ID
 */
void UGameplayMessageSubsystem::UnregisterListenerInternal(FGameplayTag Channel, int64 ListenerId) {
    check(IsInGameThread());
    if (Impl.IsValid()) {
        Impl->Registry.Unregister(Channel, ListenerId);
    }
}

/**
 * @brief 内部消息广播核心派发函数（栈内联快照 + 重入失活守卫）
 * @param Channel 广播通道
 * @param StructType 载荷反射结构体类型
 * @param Payload 载荷内存裸指针
 * @return 广播派发成功返回 Success，参数校验失败返回带 Game::ModuleId::Message 的 Failure
 */
TGameStatusResult<void> UGameplayMessageSubsystem::BroadcastMessageInternal(
    FGameplayTag Channel,
    const UScriptStruct* StructType,
    const void* Payload
) {
    check(IsInGameThread());
    if (!Channel.IsValid()) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameMessage, Warning, TEXT("【消息子系统】广播消息失败：通道标签无效。"));
        }
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Message,
            Game::MessageErrorCodes::InvalidChannel,
            TEXT("消息通道标签无效")
        );
    }
    if (StructType == nullptr) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameMessage, Warning, TEXT("【消息子系统】广播消息失败：结构体类型为空。"));
        }
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Message,
            Game::MessageErrorCodes::InvalidStructType,
            TEXT("消息结构体类型为空")
        );
    }
    if (Payload == nullptr) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameMessage, Warning, TEXT("【消息子系统】广播消息失败：载荷指针为空。"));
        }
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Message,
            Game::MessageErrorCodes::InvalidPayload,
            TEXT("载荷数据指针为空")
        );
    }
    if (!Impl.IsValid()) {
        return TGameStatusResult<void>::Success();
    }

    Game::Message::Private::FMessageListenerRegistry::FCallbackSnapshot CallbacksToInvoke;
    Impl->Registry.CollectMatchingCallbacks(Channel, StructType, CallbacksToInvoke);

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(
            LogGameMessage,
            Verbose,
            TEXT("【消息子系统】广播消息: 通道 [%s], 载荷类型=[%s], 匹配触发监听器数=%d"),
            *Channel.ToString(),
            *StructType->GetName(),
            CallbacksToInvoke.Num()
        );
    }

    for (const TSharedPtr<Game::Message::Private::FMessageListenerRegistry::FCallbackBox, ESPMode::NotThreadSafe>& CallbackBox : CallbacksToInvoke) {
        if (CallbackBox->bActive) {
            CallbackBox->Callback(Channel, StructType, Payload);
        }
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 内部跨线程待广播任务入队调度函数
 * @param BroadcastTask 待在游戏线程执行的广播任务
 * @return 入队成功返回 Success，未初始化或关闭态返回 Failure
 */
TGameStatusResult<void> UGameplayMessageSubsystem::EnqueuePendingBroadcast(TUniqueFunction<void()> BroadcastTask) {
    if (!Impl.IsValid()) {
        if (IsInGameThread()) {
            return EnsureImpl().DispatchQueue.Enqueue(MoveTemp(BroadcastTask));
        }
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Message,
            Game::MessageErrorCodes::SubsystemShuttingDown,
            TEXT("子系统尚未初始化")
        );
    }
    return Impl->DispatchQueue.Enqueue(MoveTemp(BroadcastTask));
}
