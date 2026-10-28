// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Message/GameplayMessageTypes.h"

#include "GameplayMessageSubsystem.generated.h"

/**
 * @brief 全局解耦消息总线子系统（基于 GameplayTag 寻址与 UScriptStruct 强类型校验）
 */
UCLASS()
class GAMECORE_API UGameplayMessageSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

private:
    friend struct FGameplayMessageListenerHandle;
    friend class UAsyncAction_ListenForGameplayMessage;

    using FRawMessageCallback = TFunction<void(FGameplayTag, const UScriptStruct*, const void*)>;

    struct FListenerEntry {
        int64 Id = 0;
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch;
        TWeakObjectPtr<const UScriptStruct> ExpectedStructType;
        FRawMessageCallback Callback;
    };

    struct FChannelListenerList {
        TArray<FListenerEntry> Listeners;
    };

public:
    /**
     * @brief 获取子系统单例引用（断言有效）
     * @param WorldContextObject 关联的世界上下文对象
     * @return 子系统引用
     */
    static UGameplayMessageSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取子系统指针（可空）
     * @param WorldContextObject 关联的世界上下文对象
     * @return 子系统指针，未就绪时返回 nullptr
     */
    static UGameplayMessageSubsystem* GetPtr(const UObject* WorldContextObject);

    /** 子系统销毁前清空全部通道监听 */
    void Deinitialize() override;

    /**
     * @brief 检查指定通道是否存在任何监听器
     * @param Channel 目标通道
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return 存在匹配监听器返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Message")
    bool HasListener(FGameplayTag Channel, EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch) const;

    /**
     * @brief 广播强类型结构体消息
     * @tparam TMessageStructType 载荷结构体类型（需为 USTRUCT）
     * @param Channel 目标广播通道 GameplayTag
     * @param Message 消息载荷实例
     */
    template <typename TMessageStructType>
    void BroadcastMessage(FGameplayTag Channel, const TMessageStructType& Message);

    /**
     * @brief 注册成员函数监听器（接收通道 Tag 与 Payload 引用）
     * @tparam TMessageStructType 载荷结构体类型
     * @tparam TOwner 监听宿主类型
     * @param Channel 监听通道
     * @param ListenerObject 监听宿主实例
     * @param CallbackMethod 回调成员函数指针
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return RAII 订阅句柄
     */
    template <typename TMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(FGameplayTag, const TMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 注册成员函数监听器（仅接收 Payload 引用）
     * @tparam TMessageStructType 载荷结构体类型
     * @tparam TOwner 监听宿主类型
     * @param Channel 监听通道
     * @param ListenerObject 监听宿主实例
     * @param CallbackMethod 回调成员函数指针
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return RAII 订阅句柄
     */
    template <typename TMessageStructType, typename TOwner>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TOwner* ListenerObject,
        void (TOwner::*CallbackMethod)(const TMessageStructType&),
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 注册通用 Lambda / TFunction 监听器
     * @tparam TMessageStructType 载荷结构体类型
     * @param Channel 监听通道
     * @param Callback 回调闭包
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return RAII 订阅句柄
     */
    template <typename TMessageStructType>
    [[nodiscard]] FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<void(FGameplayTag, const TMessageStructType&)> Callback,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 手动注销指定通道上的监听器 ID
     * @param Channel 目标通道
     * @param ListenerId 监听器唯一 ID
     */
    void UnregisterListener(FGameplayTag Channel, int64 ListenerId);

    /**
     * @brief 查询当前已注册的监听器总数（供测试或诊断使用）
     * @return 监听器数量
     */
    [[nodiscard]] int32 GetListenerCount() const;

private:
    FGameplayMessageListenerHandle RegisterListenerInternal(
        FGameplayTag Channel,
        const UScriptStruct* StructType,
        FRawMessageCallback Callback,
        EGameplayMessageMatch MatchRule
    );

    void BroadcastMessageInternal(
        FGameplayTag Channel,
        const UScriptStruct* StructType,
        const void* Payload
    );

private:
    TMap<FGameplayTag, FChannelListenerList> ListenerMap;
    int64 NextListenerId = 0;
};

// ---------------------------------------------------------------------------
// 模板实现（定义全部置于类外，随头文件对使用方可见）
// ---------------------------------------------------------------------------

template <typename TMessageStructType>
void UGameplayMessageSubsystem::BroadcastMessage(FGameplayTag Channel, const TMessageStructType& Message) {
    const UScriptStruct* StructType = TBaseStructure<TMessageStructType>::Get();
    BroadcastMessageInternal(Channel, StructType, &Message);
}

template <typename TMessageStructType, typename TOwner>
FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListener(
    FGameplayTag Channel,
    TOwner* ListenerObject,
    void (TOwner::*CallbackMethod)(FGameplayTag, const TMessageStructType&),
    EGameplayMessageMatch MatchRule
) {
    check(ListenerObject != nullptr);
    TWeakObjectPtr<TOwner> WeakListener(ListenerObject);
    return RegisterListener<TMessageStructType>(
        Channel,
        [WeakListener, CallbackMethod](FGameplayTag InChannel, const TMessageStructType& Payload) {
            if (TOwner* StrongListener = WeakListener.Get()) {
                (StrongListener->*CallbackMethod)(InChannel, Payload);
            }
        },
        MatchRule
    );
}

template <typename TMessageStructType, typename TOwner>
FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListener(
    FGameplayTag Channel,
    TOwner* ListenerObject,
    void (TOwner::*CallbackMethod)(const TMessageStructType&),
    EGameplayMessageMatch MatchRule
) {
    check(ListenerObject != nullptr);
    TWeakObjectPtr<TOwner> WeakListener(ListenerObject);
    return RegisterListener<TMessageStructType>(
        Channel,
        [WeakListener, CallbackMethod](FGameplayTag, const TMessageStructType& Payload) {
            if (TOwner* StrongListener = WeakListener.Get()) {
                (StrongListener->*CallbackMethod)(Payload);
            }
        },
        MatchRule
    );
}

template <typename TMessageStructType>
FGameplayMessageListenerHandle UGameplayMessageSubsystem::RegisterListener(
    FGameplayTag Channel,
    TFunction<void(FGameplayTag, const TMessageStructType&)> Callback,
    EGameplayMessageMatch MatchRule
) {
    const UScriptStruct* StructType = TBaseStructure<TMessageStructType>::Get();
    auto Thunk = [Callback = MoveTemp(Callback)](FGameplayTag InChannel, const UScriptStruct*, const void* InPayload) {
        const TMessageStructType& TypedPayload = *reinterpret_cast<const TMessageStructType*>(InPayload);
        Callback(InChannel, TypedPayload);
    };

    return RegisterListenerInternal(Channel, StructType, MoveTemp(Thunk), MatchRule);
}

