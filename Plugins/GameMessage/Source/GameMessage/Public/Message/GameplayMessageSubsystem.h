/**
 * @file GameplayMessageSubsystem.h
 * @brief 全局解耦消息总线子系统公共接口声明（Pimpl 隐藏内部实现、零 CDO 堆分配、单层 Thunk 与编译期概念约束）
 */

#pragma once

#include <type_traits>

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Message/FGameplayMessageListenerHandle.h"
#include "Message/GameplayMessageTypes.h"
#include "Message/MessageErrorCodes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SharedPointer.h"
#include "Types/TGameResult.h"

#include "GameplayMessageSubsystem.generated.h"

/**
 * @brief 全局解耦消息总线子系统（基于 GameplayTag 层级寻址、UScriptStruct 强类型校验、零闭包拷贝快照与跨线程安全入队）
 */
UCLASS()
class GAMEMESSAGE_API UGameplayMessageSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 默认构造函数（保持 Pimpl 为空，实现 CDO 零堆分配）
     */
    UGameplayMessageSubsystem();

    /**
     * @brief 析构函数（释放内部 Pimpl 资源）
     */
    ~UGameplayMessageSubsystem() override;

    /**
     * @brief 获取子系统单例引用（断言有效）
     * @param WorldContextObject 关联的世界上下文对象（支持 UWorld、UGameInstance、UGameInstanceSubsystem 及场景对象）
     * @return 子系统引用
     */
    static UGameplayMessageSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取子系统指针（可空）
     * @param WorldContextObject 关联的世界上下文对象（支持 UWorld、UGameInstance、UGameInstanceSubsystem 及场景对象）
     * @return 子系统指针，未就绪时返回 nullptr
     */
    static UGameplayMessageSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 子系统初始化生命周期回调：初始化内部注册表与跨线程派发队列
     * @param Collection 子系统集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统销毁生命周期回调：关闭跨线程入队通道、丢弃残留任务、失活并清空所有监听器且作废旧句柄纪元
     */
    void Deinitialize() override;

    /**
     * @brief 检查向目标通道广播时是否存在可命中的活跃监听器（与 BroadcastMessage 路由完全一致，自动覆盖祖先通道 PartialMatch）
     * @param Channel 目标广播通道
     * @return 存在可命中监听器返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameMessage|Message")
    bool HasListener(FGameplayTag Channel) const;

    /**
     * @brief 在主线程立即广播强类型反射结构体消息
     * @tparam TMessageStructType 满足 CGameplayMessageStruct 的载荷结构体类型
     * @param Channel 目标广播通道 GameplayTag
     * @param Message 消息载荷实例
     * @return 广播成功返回 Success，通道无效或反射元数据为空返回带 Game::ModuleId::Message 的 Failure
     */
    template <CGameplayMessageStruct TMessageStructType>
    TGameStatusResult<void> BroadcastMessage(FGameplayTag Channel, const TMessageStructType& Message);

    /**
     * @brief 线程安全异步入队强类型消息（支持任意工作线程调用，经无锁 MPSC 队列合并投递至 GameThread 串行派发）
     * @tparam TMessageStructType 满足 CGameplayMessageStruct 的载荷结构体类型
     * @param Channel 目标广播通道 GameplayTag
     * @param Message 消息载荷实例（按值移动捕获）
     * @return 成功入队返回 Success，通道无效或子系统关闭返回带 Game::ModuleId::Message 的 Failure
     */
    template <CGameplayMessageStruct TMessageStructType>
    TGameStatusResult<void> EnqueueMessage(FGameplayTag Channel, TMessageStructType Message);

    /**
     * @brief 在主线程立即排空并派发所有跨线程待处理消息队列
     * @return 本次排空实际派发的消息数量
     */
    int32 FlushPendingMessages();

    /**
     * @brief 注册 UObject 成员函数监听器（接收通道 Tag 与载荷引用，宿主销毁后自动安全跳过）
     * @tparam TMessageStructType 满足 CGameplayMessageStruct 的载荷结构体类型
     * @tparam TOwner 监听宿主 UObject 派生类型
     * @param Channel 监听通道
     * @param ListenerObject 监听宿主实例
     * @param CallbackMethod 回调成员函数指针
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return RAII 订阅句柄
     */
    template <CGameplayMessageStruct TMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(FGameplayTag, const TMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 注册 UObject 成员函数监听器（仅接收载荷引用，宿主销毁后自动安全跳过）
     * @tparam TMessageStructType 满足 CGameplayMessageStruct 的载荷结构体类型
     * @tparam TOwner 监听宿主 UObject 派生类型
     * @param Channel 监听通道
     * @param ListenerObject 监听宿主实例
     * @param CallbackMethod 回调成员函数指针
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return RAII 订阅句柄
     */
    template <CGameplayMessageStruct TMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(const TMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 注册通用 Lambda / TFunction 监听器（接收通道 Tag 与载荷引用）
     * @tparam TMessageStructType 满足 CGameplayMessageStruct 的载荷结构体类型
     * @param Channel 监听通道
     * @param Callback 回调闭包
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return RAII 订阅句柄
     */
    template <CGameplayMessageStruct TMessageStructType>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<void(FGameplayTag, const TMessageStructType&)> Callback,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 注册通用 Lambda / TFunction 监听器（仅接收载荷引用）
     * @tparam TMessageStructType 满足 CGameplayMessageStruct 的载荷结构体类型
     * @param Channel 监听通道
     * @param Callback 回调闭包
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return RAII 订阅句柄
     */
    template <CGameplayMessageStruct TMessageStructType>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<void(const TMessageStructType&)> Callback,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 查询当前已注册的活跃监听器总数（O(1)，供测试或诊断使用）
     * @return 监听器数量
     */
    [[nodiscard]] int32 GetListenerCount() const;

private:
    friend struct FGameplayMessageListenerHandle;
    friend class UAsyncAction_ListenForGameplayMessage;

    struct FImpl;
    using FRawMessageCallback = TFunction<void(FGameplayTag, const UScriptStruct*, const void*)>;

    FImpl& EnsureImpl();

    FGameplayMessageListenerHandle RegisterListenerInternal(
        FGameplayTag Channel,
        const UScriptStruct* StructType,
        FRawMessageCallback Callback,
        EGameplayMessageMatch MatchRule
    );

    void UnregisterListenerInternal(FGameplayTag Channel, int64 ListenerId);

    TGameStatusResult<void> BroadcastMessageInternal(
        FGameplayTag Channel,
        const UScriptStruct* StructType,
        const void* Payload
    );

    TGameStatusResult<void> EnqueuePendingBroadcast(TUniqueFunction<void()> BroadcastTask);

private:
    TSharedPtr<FImpl> Impl;
};

template <CGameplayMessageStruct TMessageStructType>
TGameStatusResult<void> UGameplayMessageSubsystem::BroadcastMessage(FGameplayTag Channel, const TMessageStructType& Message) {
    const UScriptStruct* StructType = TBaseStructure<TMessageStructType>::Get();
    return BroadcastMessageInternal(Channel, StructType, &Message);
}

template <CGameplayMessageStruct TMessageStructType>
TGameStatusResult<void> UGameplayMessageSubsystem::EnqueueMessage(FGameplayTag Channel, TMessageStructType Message) {
    if (!Channel.IsValid()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Message,
            Game::MessageErrorCodes::InvalidChannel,
            TEXT("消息通道标签无效")
        );
    }
    return EnqueuePendingBroadcast(
        [this, Channel, CapturedMessage = MoveTemp(Message)]() {
            BroadcastMessage(Channel, CapturedMessage);
        }
    );
}

template <CGameplayMessageStruct TMessageStructType, typename TOwner>
FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListener(
    FGameplayTag Channel,
    TOwner* ListenerObject,
    void (TOwner::*CallbackMethod)(FGameplayTag, const TMessageStructType&),
    EGameplayMessageMatch MatchRule
) {
    static_assert(std::is_base_of_v<UObject, TOwner>, "TOwner must derive from UObject for weak pointer lifecycle tracking.");
    if (ListenerObject == nullptr || CallbackMethod == nullptr) {
        return FGameplayMessageListenerHandle();
    }

    const UScriptStruct* StructType = TBaseStructure<TMessageStructType>::Get();
    TWeakObjectPtr<TOwner> WeakListener(ListenerObject);
    auto Thunk = [WeakListener, CallbackMethod](FGameplayTag InChannel, const UScriptStruct*, const void* InPayload) {
        if (TOwner* StrongListener = WeakListener.Get()) {
            const TMessageStructType& TypedPayload = *static_cast<const TMessageStructType*>(InPayload);
            (StrongListener->*CallbackMethod)(InChannel, TypedPayload);
        }
    };
    return RegisterListenerInternal(Channel, StructType, MoveTemp(Thunk), MatchRule);
}

template <CGameplayMessageStruct TMessageStructType, typename TOwner>
FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListener(
    FGameplayTag Channel,
    TOwner* ListenerObject,
    void (TOwner::*CallbackMethod)(const TMessageStructType&),
    EGameplayMessageMatch MatchRule
) {
    static_assert(std::is_base_of_v<UObject, TOwner>, "TOwner must derive from UObject for weak pointer lifecycle tracking.");
    if (ListenerObject == nullptr || CallbackMethod == nullptr) {
        return FGameplayMessageListenerHandle();
    }

    const UScriptStruct* StructType = TBaseStructure<TMessageStructType>::Get();
    TWeakObjectPtr<TOwner> WeakListener(ListenerObject);
    auto Thunk = [WeakListener, CallbackMethod]([[maybe_unused]] FGameplayTag InChannel, const UScriptStruct*, const void* InPayload) {
        if (TOwner* StrongListener = WeakListener.Get()) {
            const TMessageStructType& TypedPayload = *static_cast<const TMessageStructType*>(InPayload);
            (StrongListener->*CallbackMethod)(TypedPayload);
        }
    };
    return RegisterListenerInternal(Channel, StructType, MoveTemp(Thunk), MatchRule);
}

template <CGameplayMessageStruct TMessageStructType>
FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListener(
    FGameplayTag Channel,
    TFunction<void(FGameplayTag, const TMessageStructType&)> Callback,
    EGameplayMessageMatch MatchRule
) {
    if (!Callback) {
        return FGameplayMessageListenerHandle();
    }

    const UScriptStruct* StructType = TBaseStructure<TMessageStructType>::Get();
    auto Thunk = [Callback = MoveTemp(Callback)](FGameplayTag InChannel, const UScriptStruct*, const void* InPayload) {
        const TMessageStructType& TypedPayload = *static_cast<const TMessageStructType*>(InPayload);
        Callback(InChannel, TypedPayload);
    };
    return RegisterListenerInternal(Channel, StructType, MoveTemp(Thunk), MatchRule);
}

template <CGameplayMessageStruct TMessageStructType>
FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListener(
    FGameplayTag Channel,
    TFunction<void(const TMessageStructType&)> Callback,
    EGameplayMessageMatch MatchRule
) {
    if (!Callback) {
        return FGameplayMessageListenerHandle();
    }

    const UScriptStruct* StructType = TBaseStructure<TMessageStructType>::Get();
    auto Thunk = [Callback = MoveTemp(Callback)]([[maybe_unused]] FGameplayTag InChannel, const UScriptStruct*, const void* InPayload) {
        const TMessageStructType& TypedPayload = *static_cast<const TMessageStructType*>(InPayload);
        Callback(TypedPayload);
    };
    return RegisterListenerInternal(Channel, StructType, MoveTemp(Thunk), MatchRule);
}
