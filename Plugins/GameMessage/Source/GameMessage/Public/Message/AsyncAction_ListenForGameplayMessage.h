/**
 * @file AsyncAction_ListenForGameplayMessage.h
 * @brief 蓝图异步监听 GameplayTag 消息动作节点声明（基于 FStructOnScope 实现对齐安全与全属性 GC 追踪）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Message/FGameplayMessageListenerHandle.h"
#include "Message/GameplayMessageTypes.h"
#include "UObject/StructOnScope.h"

#include "AsyncAction_ListenForGameplayMessage.generated.h"

class UGameplayMessageSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncGameplayMessageDelegate, FGameplayTag, ActualChannel);

/**
 * @brief 异步监听 GameplayTag 消息的通用蓝图动作节点
 */
UCLASS()
class GAMEMESSAGE_API UAsyncAction_ListenForGameplayMessage : public UBlueprintAsyncActionBase {
    GENERATED_BODY()

public:
    /**
     * @brief 收到匹配消息时触发的蓝图执行引脚
     */
    UPROPERTY(BlueprintAssignable)
    FAsyncGameplayMessageDelegate OnMessageReceived;

    /**
     * @brief 异步监听指定通道的消息广播（蓝图可直接使用）
     * @param WorldContextObject 关联的世界上下文对象
     * @param Channel 监听通道 GameplayTag
     * @param MatchRule 匹配模式（默认精确匹配）
     * @return 异步操作对象，世界上下文无效时返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GameMessage|Message", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
    static UAsyncAction_ListenForGameplayMessage* ListenForGameplayMessages(
        UObject* WorldContextObject,
        FGameplayTag Channel,
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch
    );

    /**
     * @brief 向 UE GC 报告缓存载荷结构体及其内部全部 UObject 强引用，防止悬垂指针与动态结构体误回收
     * @param InThis 当前动作节点实例
     * @param Collector GC 引用收集器
     */
    static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

    /**
     * @brief 节点激活回调（幂等）：注册通道消息监听并将命中消息转发至 OnMessageReceived 委托
     */
    void Activate() override;

    /**
     * @brief 终止异步操作、安全注销监听句柄并销毁缓存载荷
     */
    void SetReadyToDestroy() override;

    /**
     * @brief UObject 销毁兜底回调：确保未经显式 Cancel 回收时亦释放监听句柄与缓存结构体资源
     */
    void BeginDestroy() override;

    /**
     * @brief 手动终止并清理异步动作节点（供蓝图调用）
     */
    UFUNCTION(BlueprintCallable, Category = "GameMessage|Message", meta = (DisplayName = "Cancel Message Listener"))
    void Cancel();

    /**
     * @brief 提取上一次接收到的强类型载荷结构体（蓝图通配符引脚）
     * @param OutPayload 接收载荷的结构体变量引用
     * @return 成功提取且类型匹配返回 true，类型不匹配或无载荷返回 false
     */
    UFUNCTION(BlueprintCallable, CustomThunk, Category = "GameMessage|Message", meta = (CustomStructureParam = "OutPayload"))
    bool GetPayload(UPARAM(ref) int32& OutPayload);
    DECLARE_FUNCTION(execGetPayload);

    /**
     * @brief C++ 辅助方法：从上一次收到的消息中提取强类型载荷结构体（支持 USTRUCT 与引擎核心结构体）
     * @tparam TMessageStructType 满足 CGameplayMessageStruct 的目标载荷结构体类型
     * @param OutPayload 接收载荷的结构体引用
     * @return 成功提取且类型匹配返回 true，类型不匹配或无载荷返回 false
     */
    template <CGameplayMessageStruct TMessageStructType>
    bool GetPayloadTyped(TMessageStructType& OutPayload) const {
        return CopyReceivedPayloadTo(TBaseStructure<TMessageStructType>::Get(), &OutPayload);
    }

private:
    /**
     * @brief 主线程消息命中内部处理函数：刷新对齐缓存并广播蓝图引脚
     * @param InChannel 实际广播通道
     * @param StructType 载荷反射结构体类型
     * @param PayloadPtr 载荷数据内存地址
     */
    void HandleMessageReceived(FGameplayTag InChannel, const UScriptStruct* StructType, const void* PayloadPtr);

    /**
     * @brief 将当前缓存载荷按反射类型兼容性拷贝至目标地址
     * @param TargetStruct 目标结构体反射元数据
     * @param DestAddress 目标实例内存地址
     * @return 类型兼容且拷贝成功返回 true
     */
    [[nodiscard]] bool CopyReceivedPayloadTo(const UScriptStruct* TargetStruct, void* DestAddress) const;

private:
    TWeakObjectPtr<UObject> WorldContextObjectWeak;
    FGameplayTag TargetChannel;
    EGameplayMessageMatch TargetMatchRule = EGameplayMessageMatch::ExactMatch;
    FGameplayMessageListenerHandle ListenerHandle;
    FStructOnScope ReceivedPayload;
};
