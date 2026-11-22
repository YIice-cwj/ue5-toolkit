/**
 * @file FMessageListenerRegistry.cpp
 * @brief 消息总线通道监听器注册表实现（零堆分配祖先链遍历、单次弱引用解析与重入安全失活控制）
 */

#include "Message/FMessageListenerRegistry.h"

#include "Config/GameCompileSwitches.h"
#include "GameMessage.h"
#include "GameplayTagsManager.h"

namespace Game::Message::Private {

/**
 * @brief 构造函数：初始化首个运行纪元
 */
FMessageListenerRegistry::FMessageListenerRegistry()
    : ActiveEpoch(MakeShared<uint8>(0)) {
}

/**
 * @brief 开启新的子系统运行纪元（Initialize 时调用，使新签发句柄绑定至最新纪元）
 */
void FMessageListenerRegistry::BeginEpoch() {
    if (!ActiveEpoch.IsValid()) {
        ActiveEpoch = MakeShared<uint8>(0);
    }
}

/**
 * @brief 注册一个类型擦除通道监听器
 * @param Channel 目标通道标签
 * @param StructType 期望匹配的反射结构体元数据指针（为空表示通配）
 * @param Callback 底层类型擦除回调闭包
 * @param MatchRule 精确或父子前缀匹配规则
 * @return 监听器 ID 与当前运行纪元弱引用
 */
FMessageListenerRegistry::FRegisterReceipt FMessageListenerRegistry::Register(
    FGameplayTag Channel,
    const UScriptStruct* StructType,
    FRawMessageCallback Callback,
    EGameplayMessageMatch MatchRule
) {
    check(ActiveEpoch.IsValid());

    const int64 ListenerId = ++NextListenerId;
    ++TotalListenerCount;

    FChannelListenerList& List = ListenerMap.FindOrAdd(Channel);
    if (MatchRule == EGameplayMessageMatch::PartialMatch) {
        ++PartialMatchListenerCount;
        ++List.PartialMatchCount;
    }

    List.Listeners.Add(FListenerEntry{
        ListenerId,
        MatchRule,
        StructType == nullptr,
        StructType,
        MakeShared<FCallbackBox, ESPMode::NotThreadSafe>(MoveTemp(Callback))
    });

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(
            LogGameMessage,
            Verbose,
            TEXT("【消息子系统】注册监听器成功: 通道 [%s], 监听器 ID=%lld, 结构体=[%s], 匹配规则=%d"),
            *Channel.ToString(),
            ListenerId,
            StructType ? *StructType->GetName() : TEXT("None"),
            static_cast<int32>(MatchRule)
        );
    }

    return FRegisterReceipt{ListenerId, ActiveEpoch};
}

/**
 * @brief 按通道与唯一 ID 注销监听器，并立即置位对应 CallbackBox 失活标记以防同轮广播误触发
 * @param Channel 目标通道标签
 * @param ListenerId 监听器唯一 ID
 */
void FMessageListenerRegistry::Unregister(FGameplayTag Channel, int64 ListenerId) {
    check(ListenerId != 0);

    FChannelListenerList* List = ListenerMap.Find(Channel);
    if (List == nullptr) {
        return;
    }

    const int32 FoundIndex = List->Listeners.IndexOfByPredicate([ListenerId](const FListenerEntry& Entry) {
        return Entry.Id == ListenerId;
    });
    if (FoundIndex == INDEX_NONE) {
        return;
    }

    FListenerEntry& TargetEntry = List->Listeners[FoundIndex];
    TargetEntry.CallbackBox->bActive = false;
    if (TargetEntry.MatchRule == EGameplayMessageMatch::PartialMatch) {
        --PartialMatchListenerCount;
        --List->PartialMatchCount;
    }
    --TotalListenerCount;
    List->Listeners.RemoveAt(FoundIndex);

    if (List->Listeners.IsEmpty()) {
        ListenerMap.Remove(Channel);
    }

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(
            LogGameMessage,
            Verbose,
            TEXT("【消息子系统】注销监听器成功: 通道 [%s], 监听器 ID=%lld"),
            *Channel.ToString(),
            ListenerId
        );
    }
}

/**
 * @brief 清空全部通道监听器、失活全部在途快照外壳并作废当前运行纪元
 * @return 本次清空涉及的通道总数
 */
int32 FMessageListenerRegistry::ClearAll() {
    const int32 ChannelCount = ListenerMap.Num();
    for (TPair<FGameplayTag, FChannelListenerList>& Pair : ListenerMap) {
        for (FListenerEntry& Entry : Pair.Value.Listeners) {
            Entry.CallbackBox->bActive = false;
        }
    }

    ListenerMap.Empty();
    ActiveEpoch.Reset();
    NextListenerId = 0;
    PartialMatchListenerCount = 0;
    TotalListenerCount = 0;
    return ChannelCount;
}

/**
 * @brief 检查向目标通道广播时是否存在至少一个可命中的活跃监听器（含祖先通道 PartialMatch）
 * @param Channel 目标广播通道
 * @return 存在匹配监听器返回 true
 */
bool FMessageListenerRegistry::HasListener(FGameplayTag Channel) const {
    if (!Channel.IsValid()) {
        return false;
    }

    if (const FChannelListenerList* ExactList = ListenerMap.Find(Channel)) {
        if (!ExactList->Listeners.IsEmpty()) {
            return true;
        }
    }

    if (PartialMatchListenerCount <= 0) {
        return false;
    }

    bool bFoundParentListener = false;
    ForEachParentChannel(Channel, [&bFoundParentListener](const FChannelListenerList& ParentList) {
        if (ParentList.PartialMatchCount > 0) {
            bFoundParentListener = true;
            return false;
        }
        return true;
    });
    return bFoundParentListener;
}

/**
 * @brief 收集目标通道与反射类型匹配的全部活跃监听器回调快照（零闭包深拷贝）
 * @param Channel 目标广播通道
 * @param StructType 广播载荷反射结构体类型
 * @param OutCallbacks 输出的栈内联回调外壳快照列表
 */
void FMessageListenerRegistry::CollectMatchingCallbacks(
    FGameplayTag Channel,
    const UScriptStruct* StructType,
    FCallbackSnapshot& OutCallbacks
) const {
    if (const FChannelListenerList* ExactList = ListenerMap.Find(Channel)) {
        for (const FListenerEntry& Entry : ExactList->Listeners) {
            if (IsEntryCompatible(Entry, StructType)) {
                OutCallbacks.Add(Entry.CallbackBox);
            }
        }
    }

    if (PartialMatchListenerCount <= 0) {
        return;
    }

    ForEachParentChannel(Channel, [&OutCallbacks, StructType](const FChannelListenerList& ParentList) {
        if (ParentList.PartialMatchCount <= 0) {
            return true;
        }
        for (const FListenerEntry& Entry : ParentList.Listeners) {
            if (Entry.MatchRule == EGameplayMessageMatch::PartialMatch && IsEntryCompatible(Entry, StructType)) {
                OutCallbacks.Add(Entry.CallbackBox);
            }
        }
        return true;
    });
}

/**
 * @brief 判断监听条目与给定载荷结构体类型是否兼容（通配、精确一致或子类；单次弱引用解析）
 * @param Entry 监听条目
 * @param StructType 广播载荷反射结构体类型
 * @return 类型兼容返回 true
 */
bool FMessageListenerRegistry::IsEntryCompatible(const FListenerEntry& Entry, const UScriptStruct* StructType) noexcept {
    if (Entry.bWildcard) {
        return true;
    }
    const UScriptStruct* ExpectedStruct = Entry.ExpectedStructType.Get();
    return ExpectedStruct != nullptr && (StructType == ExpectedStruct || StructType->IsChildOf(ExpectedStruct));
}

/**
 * @brief 通过 GameplayTag 树节点指针链零堆分配遍历目标通道的全部严格祖先通道
 * @tparam TVisitor 接收 const FChannelListenerList& 并返回 bool（返回 false 提前停止）的访问者
 * @param Channel 目标子通道
 * @param Visitor 祖先通道列表访问回调
 */
template <typename TVisitor>
void FMessageListenerRegistry::ForEachParentChannel(FGameplayTag Channel, TVisitor&& Visitor) const {
    const TSharedPtr<FGameplayTagNode> TagNode = UGameplayTagsManager::Get().FindTagNode(Channel);
    if (!TagNode.IsValid()) {
        return;
    }

    for (TSharedPtr<FGameplayTagNode> ParentNode = TagNode->GetParentTagNode();
         ParentNode.IsValid();
         ParentNode = ParentNode->GetParentTagNode()) {
        const FGameplayTag& ParentTag = ParentNode->GetCompleteTag();
        if (!ParentTag.IsValid()) {
            break;
        }
        const FChannelListenerList* ParentList = ListenerMap.Find(ParentTag);
        if (ParentList == nullptr) {
            continue;
        }
        if (!Visitor(*ParentList)) {
            break;
        }
    }
}

}  // namespace Game::Message::Private
