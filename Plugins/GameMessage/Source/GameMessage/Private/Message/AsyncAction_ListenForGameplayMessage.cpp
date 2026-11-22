/**
 * @file AsyncAction_ListenForGameplayMessage.cpp
 * @brief 蓝图异步监听 GameplayTag 消息动作节点实现（FStructOnScope 对齐缓存与 GC 追踪）
 */

#include "Message/AsyncAction_ListenForGameplayMessage.h"

#include "Message/GameplayMessageSubsystem.h"
#include "UObject/UnrealType.h"

/**
 * @brief 异步监听指定通道的消息广播（蓝图可直接使用）
 * @param WorldContextObject 关联的世界上下文对象
 * @param Channel 监听通道 GameplayTag
 * @param MatchRule 匹配模式（默认精确匹配）
 * @return 异步操作对象，世界上下文无效时返回 nullptr
 */
UAsyncAction_ListenForGameplayMessage* UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
    UObject* WorldContextObject,
    FGameplayTag Channel,
    EGameplayMessageMatch MatchRule
) {
    if (WorldContextObject == nullptr) {
        return nullptr;
    }

    UAsyncAction_ListenForGameplayMessage* Action = NewObject<UAsyncAction_ListenForGameplayMessage>();
    Action->WorldContextObjectWeak = WorldContextObject;
    Action->TargetChannel = Channel;
    Action->TargetMatchRule = MatchRule;
    if (UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(WorldContextObject)) {
        Action->RegisterWithGameInstance(Subsystem->GetGameInstance());
    }
    return Action;
}

/**
 * @brief 向 UE GC 报告缓存载荷结构体及其内部全部 UObject 强引用，防止悬垂指针与动态结构体误回收
 * @param InThis 当前动作节点实例
 * @param Collector GC 引用收集器
 */
void UAsyncAction_ListenForGameplayMessage::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector) {
    UAsyncAction_ListenForGameplayMessage* This = CastChecked<UAsyncAction_ListenForGameplayMessage>(InThis);
    This->ReceivedPayload.AddReferencedObjects(Collector);
    Super::AddReferencedObjects(InThis, Collector);
}

/**
 * @brief 节点激活回调（幂等）：注册通道消息监听并将命中消息转发至 OnMessageReceived 委托
 */
void UAsyncAction_ListenForGameplayMessage::Activate() {
    if (ListenerHandle.IsValid()) {
        return;
    }
    if (!TargetChannel.IsValid()) {
        SetReadyToDestroy();
        return;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(WorldContextObjectWeak.Get());
    if (Subsystem == nullptr) {
        SetReadyToDestroy();
        return;
    }

    ListenerHandle = Subsystem->RegisterListenerInternal(
        TargetChannel,
        nullptr,
        [WeakThis = TWeakObjectPtr<UAsyncAction_ListenForGameplayMessage>(this)](
            FGameplayTag InChannel,
            const UScriptStruct* StructType,
            const void* PayloadPtr
        ) {
            if (UAsyncAction_ListenForGameplayMessage* StrongThis = WeakThis.Get()) {
                StrongThis->HandleMessageReceived(InChannel, StructType, PayloadPtr);
            }
        },
        TargetMatchRule
    );
    if (!ListenerHandle.IsValid()) {
        SetReadyToDestroy();
    }
}

/**
 * @brief 终止异步操作、安全注销监听句柄并销毁缓存载荷
 */
void UAsyncAction_ListenForGameplayMessage::SetReadyToDestroy() {
    ListenerHandle.Unregister();
    ReceivedPayload.Reset();
    Super::SetReadyToDestroy();
}

/**
 * @brief UObject 销毁兜底回调：确保未经显式 Cancel 回收时亦释放监听句柄与缓存结构体资源
 */
void UAsyncAction_ListenForGameplayMessage::BeginDestroy() {
    ListenerHandle.Unregister();
    ReceivedPayload.Reset();
    Super::BeginDestroy();
}

/**
 * @brief 手动终止并清理异步动作节点（供蓝图调用）
 */
void UAsyncAction_ListenForGameplayMessage::Cancel() {
    SetReadyToDestroy();
}

/**
 * @brief 提取上一次接收到的强类型载荷结构体原生占位声明（由 execGetPayload 接管）
 * @param OutPayload 接收载荷的结构体变量引用
 * @return 恒不直接执行
 */
bool UAsyncAction_ListenForGameplayMessage::GetPayload([[maybe_unused]] UPARAM(ref) int32& OutPayload) {
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
    if (StructProperty != nullptr && DestAddress != nullptr && P_THIS != nullptr) {
        bSuccess = P_THIS->CopyReceivedPayloadTo(StructProperty->Struct, DestAddress);
    }
    *static_cast<bool*>(RESULT_PARAM) = bSuccess;
}

/**
 * @brief 主线程消息命中内部处理函数：刷新对齐缓存并广播蓝图引脚
 * @param InChannel 实际广播通道
 * @param StructType 载荷反射结构体类型
 * @param PayloadPtr 载荷数据内存地址
 */
void UAsyncAction_ListenForGameplayMessage::HandleMessageReceived(
    FGameplayTag InChannel,
    const UScriptStruct* StructType,
    const void* PayloadPtr
) {
    if (StructType == nullptr || PayloadPtr == nullptr) {
        ReceivedPayload.Reset();
    } else {
        if (!ReceivedPayload.IsValid() || ReceivedPayload.GetStruct() != StructType) {
            ReceivedPayload.Reset();
            ReceivedPayload.Initialize(StructType);
        }
        if (uint8* DestMemory = ReceivedPayload.GetStructMemory()) {
            StructType->CopyScriptStruct(DestMemory, PayloadPtr);
        }
    }
    OnMessageReceived.Broadcast(InChannel);
}

/**
 * @brief 将当前缓存载荷按反射类型兼容性拷贝至目标地址
 * @param TargetStruct 目标结构体反射元数据
 * @param DestAddress 目标实例内存地址
 * @return 类型兼容且拷贝成功返回 true
 */
bool UAsyncAction_ListenForGameplayMessage::CopyReceivedPayloadTo(
    const UScriptStruct* TargetStruct,
    void* DestAddress
) const {
    if (TargetStruct == nullptr || DestAddress == nullptr || !ReceivedPayload.IsValid()) {
        return false;
    }

    const UScriptStruct* ReceivedStruct = Cast<UScriptStruct>(ReceivedPayload.GetStruct());
    if (ReceivedStruct == nullptr || (ReceivedStruct != TargetStruct && !ReceivedStruct->IsChildOf(TargetStruct))) {
        return false;
    }

    TargetStruct->CopyScriptStruct(DestAddress, ReceivedPayload.GetStructMemory());
    return true;
}
