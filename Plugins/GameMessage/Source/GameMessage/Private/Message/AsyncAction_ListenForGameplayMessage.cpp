// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/AsyncAction_ListenForGameplayMessage.h"
#include "Message/GameplayMessageSubsystem.h"
#include "UObject/UnrealType.h"

/**
 * @brief 异步监听指定通道的消息广播（蓝图可直接使用）
 * @param WorldContextObject 关联的世界上下文对象
 * @param Channel 监听通道 GameplayTag
 * @param MatchRule 匹配模式（默认精确匹配）
 * @return 异步操作对象
 */
UAsyncAction_ListenForGameplayMessage* UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
    UObject* WorldContextObject,
    FGameplayTag Channel,
    EGameplayMessageMatch MatchRule
) {
    if (!WorldContextObject) {
        return nullptr;
    }

    UAsyncAction_ListenForGameplayMessage* Action = NewObject<UAsyncAction_ListenForGameplayMessage>();
    Action->WorldContextObjectWeak = WorldContextObject;
    Action->TargetChannel = Channel;
    Action->TargetMatchRule = MatchRule;
    Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

/**
 * @brief 清理已缓存的接收载荷结构体实例并释放内存
 */
void UAsyncAction_ListenForGameplayMessage::ClearReceivedPayload() {
    if (ReceivedPayloadStructType.IsValid() && ReceivedPayloadData.Num() > 0) {
        ReceivedPayloadStructType->DestroyStruct(ReceivedPayloadData.GetData());
        ReceivedPayloadData.Reset();
    }
    ReceivedPayloadStructType.Reset();
}

/**
 * @brief 节点激活回调：注册通道消息监听并将命中的消息转发至 OnMessageReceived 委托
 */
void UAsyncAction_ListenForGameplayMessage::Activate() {
    if (!TargetChannel.IsValid()) {
        SetReadyToDestroy();
        return;
    }

    if (UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(WorldContextObjectWeak.Get())) {
        ListenerHandle = Subsystem->RegisterListenerInternal(
            TargetChannel,
            nullptr,
            [WeakThis = TWeakObjectPtr<UAsyncAction_ListenForGameplayMessage>(this)](
                FGameplayTag InChannel,
                const UScriptStruct* StructType,
                const void* PayloadPtr
            ) {
                if (UAsyncAction_ListenForGameplayMessage* StrongThis = WeakThis.Get()) {
                    StrongThis->ClearReceivedPayload();
                    if (StructType && PayloadPtr) {
                        StrongThis->ReceivedPayloadStructType = StructType;
                        StrongThis->ReceivedPayloadData.SetNumUninitialized(StructType->GetStructureSize());
                        StructType->InitializeStruct(StrongThis->ReceivedPayloadData.GetData());
                        StructType->CopyScriptStruct(StrongThis->ReceivedPayloadData.GetData(), PayloadPtr);
                    }
                    StrongThis->OnMessageReceived.Broadcast(InChannel);
                }
            },
            TargetMatchRule
        );
    } else {
        SetReadyToDestroy();
    }
}

/**
 * @brief 终止异步操作并安全注销监听句柄
 */
void UAsyncAction_ListenForGameplayMessage::SetReadyToDestroy() {
    ListenerHandle.Unregister();
    ClearReceivedPayload();
    Super::SetReadyToDestroy();
}

/**
 * @brief 手动终止并清理异步动作节点（供蓝图调用）
 */
void UAsyncAction_ListenForGameplayMessage::Cancel() {
    SetReadyToDestroy();
}

/**
 * @brief 提取上一次接收到的强类型载荷结构体原生 Thunk 实现
 * @param OutPayload 接收载荷的结构体变量引用
 * @return 成功提取且类型匹配返回 true，类型不匹配或无载荷返回 false
 */
bool UAsyncAction_ListenForGameplayMessage::GetPayload(UPARAM(ref) int32& OutPayload) {
    checkNoEntry();
    return false;
}

/**
 * @brief 蓝图虚拟机执行 GetPayload 字节码的 CustomThunk 解释器
 * @param Context 执行上下文对象
 * @param Stack 虚拟机求值栈帧
 * @param Z_Param__Result 返回值内存地址
 */
DEFINE_FUNCTION(UAsyncAction_ListenForGameplayMessage::execGetPayload) {
    Stack.StepCompiledIn<FStructProperty>(nullptr);
    const FStructProperty* StructProperty = CastField<FStructProperty>(Stack.MostRecentProperty);
    void* DestAddress = Stack.MostRecentPropertyAddress;

    P_FINISH;

    bool bSuccess = false;
    if (StructProperty && DestAddress && P_THIS) {
        const UScriptStruct* TargetStruct = StructProperty->Struct;
        if (P_THIS->ReceivedPayloadStructType.IsValid() && P_THIS->ReceivedPayloadStructType.Get() == TargetStruct && P_THIS->ReceivedPayloadData.Num() > 0) {
            TargetStruct->CopyScriptStruct(DestAddress, P_THIS->ReceivedPayloadData.GetData());
            bSuccess = true;
        }
    }
    *static_cast<bool*>(RESULT_PARAM) = bSuccess;
}
