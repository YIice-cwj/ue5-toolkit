/**
 * @file AsyncAction_ListenForGameplayMessageTest.cpp
 * @brief 异步消息监听蓝图节点自动化测试（覆盖载荷提取、多态、核心结构体、CustomThunk、幂等与 GC 保活）
 */

#include "Message/AsyncAction_ListenForGameplayMessage.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Tests/FTestGameplayMessage.h"
#include "Tests/UGameMessageTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Stack.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAsyncActionListenForGameplayMessageTest,
    "Game.Message.AsyncAction.Listen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 UAsyncAction_ListenForGameplayMessage 激活、多态与核心结构体载荷提取及 CustomThunk 虚拟机路径
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FAsyncActionListenForGameplayMessageTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("AsyncActionListenTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界上下文有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(World);
    TestNotNull(TEXT("子系统实例有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.AsyncAction.Listen")));
    TestFalse(TEXT("初始状态无监听器"), Subsystem->HasListener(Channel));

    UGameMessageTestReceiver* Receiver = NewObject<UGameMessageTestReceiver>(GameInstance);
    UAsyncAction_ListenForGameplayMessage* AsyncAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        Channel,
        EGameplayMessageMatch::ExactMatch
    );
    TestNotNull(TEXT("创建异步节点成功"), AsyncAction);
    if (AsyncAction == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    AsyncAction->OnMessageReceived.AddDynamic(Receiver, &UGameMessageTestReceiver::OnAsyncMessageReceived);
    AsyncAction->Activate();

    TestTrue(TEXT("激活后子系统检测到有效监听器"), Subsystem->HasListener(Channel));

    FTestGameplayMessage Msg1;
    Msg1.Value = 101;
    Msg1.Text = TEXT("FirstPacket");
    Subsystem->BroadcastMessage(Channel, Msg1);

    TestEqual(TEXT("收到首条消息"), Receiver->ReceivedCount, 1);
    TestEqual(TEXT("首条消息通道匹配"), Receiver->LastChannel, Channel);

    FTestGameplayMessage ExtractedMsg1;
    TestTrue(TEXT("可成功提取首条消息载荷"), AsyncAction->GetPayloadTyped(ExtractedMsg1));
    TestEqual(TEXT("提取首条消息 Value 匹配"), ExtractedMsg1.Value, 101);
    TestEqual(TEXT("提取首条消息 Text 匹配"), ExtractedMsg1.Text, TEXT("FirstPacket"));

    FTestGameplayMessage Msg1Reuse;
    Msg1Reuse.Value = 155;
    Msg1Reuse.Text = TEXT("ReusedSlotPacket");
    Subsystem->BroadcastMessage(Channel, Msg1Reuse);

    FTestGameplayMessage ExtractedReuse;
    TestTrue(TEXT("同类型连续消息复用缓冲区并准确覆盖提取"), AsyncAction->GetPayloadTyped(ExtractedReuse));
    TestEqual(TEXT("复用缓冲区提取 Value 匹配"), ExtractedReuse.Value, 155);
    TestEqual(TEXT("复用缓冲区提取 Text 匹配"), ExtractedReuse.Text, TEXT("ReusedSlotPacket"));

    FUnrelatedTestGameplayMessage WrongStruct;
    TestFalse(TEXT("不相关类型提取载荷必须返回 false"), AsyncAction->GetPayloadTyped(WrongStruct));

    UFunction* GetPayloadFunc = UAsyncAction_ListenForGameplayMessage::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UAsyncAction_ListenForGameplayMessage, GetPayload)
    );
    FStructProperty* OutSlotProp = FindFProperty<FStructProperty>(
        UGameMessageTestReceiver::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UGameMessageTestReceiver, ExtractedPayloadSlot)
    );
    TestNotNull(TEXT("找到 GetPayload UFunction"), GetPayloadFunc);
    TestNotNull(TEXT("找到测试结构体属性"), OutSlotProp);
    if (GetPayloadFunc != nullptr && OutSlotProp != nullptr) {
        uint8 Bytecode[1 + sizeof(FProperty*) + 1]{};
        Bytecode[0] = EX_InstanceVariable;
        FProperty* PropertyPtr = OutSlotProp;
        FMemory::Memcpy(&Bytecode[1], &PropertyPtr, sizeof(FProperty*));
        Bytecode[1 + sizeof(FProperty*)] = EX_EndFunctionParms;

        FFrame Stack(Receiver, GetPayloadFunc, Receiver);
        Stack.Code = Bytecode;
        bool bThunkSuccess = false;
        UAsyncAction_ListenForGameplayMessage::execGetPayload(AsyncAction, Stack, &bThunkSuccess);
        TestTrue(TEXT("蓝图 CustomThunk execGetPayload 成功提取载荷"), bThunkSuccess);
        TestEqual(TEXT("CustomThunk 提取 Value 一致"), Receiver->ExtractedPayloadSlot.Value, 155);
        TestEqual(TEXT("CustomThunk 提取 Text 一致"), Receiver->ExtractedPayloadSlot.Text, TEXT("ReusedSlotPacket"));
    }

    FDerivedTestGameplayMessage DerivedMsg;
    DerivedMsg.Value = 202;
    DerivedMsg.Text = TEXT("DerivedPacket");
    DerivedMsg.ExtraCode = 55;
    Subsystem->BroadcastMessage(Channel, DerivedMsg);

    TestEqual(TEXT("收到派生载荷消息"), Receiver->ReceivedCount, 3);

    FTestGameplayMessage ExtractedBase;
    TestTrue(TEXT("可按基类多态提取派生载荷"), AsyncAction->GetPayloadTyped(ExtractedBase));
    TestEqual(TEXT("派生转基类 Value 匹配"), ExtractedBase.Value, 202);
    TestEqual(TEXT("派生转基类 Text 匹配"), ExtractedBase.Text, TEXT("DerivedPacket"));

    const FVector CoreVectorPayload(1.0, 2.0, 3.0);
    Subsystem->BroadcastMessage(Channel, CoreVectorPayload);
    FVector ExtractedVector = FVector::ZeroVector;
    TestTrue(TEXT("支持提取引擎核心结构体 FVector 载荷"), AsyncAction->GetPayloadTyped(ExtractedVector));
    TestEqual(TEXT("FVector 载荷数值完全一致"), ExtractedVector, CoreVectorPayload);

    AsyncAction->SetReadyToDestroy();
    TestFalse(TEXT("销毁后监听器已解绑"), Subsystem->HasListener(Channel));

    FTestGameplayMessage ExtractedMsgAfterDestroy;
    TestFalse(TEXT("销毁后提取载荷应返回 false"), AsyncAction->GetPayloadTyped(ExtractedMsgAfterDestroy));

    Subsystem->BroadcastMessage(Channel, DerivedMsg);
    TestEqual(TEXT("销毁后不再触发回调"), Receiver->ReceivedCount, 4);

    UAsyncAction_ListenForGameplayMessage* DirectGiAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        GameInstance,
        Channel,
        EGameplayMessageMatch::ExactMatch
    );
    TestNotNull(TEXT("直接以 GameInstance 创建异步节点成功"), DirectGiAction);
    if (DirectGiAction != nullptr) {
        DirectGiAction->Activate();
        TestTrue(TEXT("GameInstance 直通节点激活后注册监听有效"), Subsystem->HasListener(Channel));
        DirectGiAction->Cancel();
        TestFalse(TEXT("GameInstance 直通节点取消后监听注销"), Subsystem->HasListener(Channel));
    }

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAsyncActionCancelAndBoundaryTest,
    "Game.Message.AsyncAction.CancelAndBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证异步动作节点手动取消、空上下文/无效通道/无子系统防守、双次 Activate 幂等性与前缀匹配模式
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FAsyncActionCancelAndBoundaryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("AsyncActionCancelTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(World);
    TestNotNull(TEXT("子系统有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    const int32 InitialListenerCount = Subsystem->GetListenerCount();
    const FGameplayTag ParentChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.AsyncAction.Parent")));
    const FGameplayTag ChildChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.AsyncAction.Parent.Child")));

    UAsyncAction_ListenForGameplayMessage* NullContextAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        nullptr,
        ParentChannel
    );
    TestNull(TEXT("空世界上下文返回 nullptr"), NullContextAction);

    UAsyncAction_ListenForGameplayMessage* InvalidTagAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        FGameplayTag()
    );
    TestNotNull(TEXT("空标签可创建节点并在 Activate 阶段自销毁"), InvalidTagAction);
    if (InvalidTagAction != nullptr) {
        InvalidTagAction->Activate();
    }
    TestEqual(TEXT("空标签激活后不残留任何新增监听器"), Subsystem->GetListenerCount(), InitialListenerCount);

    UGameMessageTestReceiver* DetachedContext = NewObject<UGameMessageTestReceiver>(GetTransientPackage());
    UAsyncAction_ListenForGameplayMessage* NoSubsystemAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        DetachedContext,
        ParentChannel
    );
    TestNotNull(TEXT("无子系统上下文可安全创建节点"), NoSubsystemAction);
    if (NoSubsystemAction != nullptr) {
        NoSubsystemAction->Activate();
    }
    TestEqual(TEXT("无子系统上下文激活后自销毁无新增残留"), Subsystem->GetListenerCount(), InitialListenerCount);
    DetachedContext->MarkAsGarbage();

    UGameMessageTestReceiver* Receiver = NewObject<UGameMessageTestReceiver>(GameInstance);
    UAsyncAction_ListenForGameplayMessage* ActionToCancel = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        ParentChannel,
        EGameplayMessageMatch::PartialMatch
    );
    TestNotNull(TEXT("创建部分匹配异步节点成功"), ActionToCancel);
    if (ActionToCancel == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    ActionToCancel->OnMessageReceived.AddDynamic(Receiver, &UGameMessageTestReceiver::OnAsyncMessageReceived);
    ActionToCancel->Activate();
    ActionToCancel->Activate();
    TestEqual(TEXT("重复调用 Activate 保持幂等且仅新增 1 个监听器"), Subsystem->GetListenerCount(), InitialListenerCount + 1);
    TestTrue(TEXT("激活后父通道存在监听器"), Subsystem->HasListener(ParentChannel));
    TestTrue(TEXT("激活后子通道 HasListener 与 PartialMatch 路由一致返回 true"), Subsystem->HasListener(ChildChannel));

    FTestGameplayMessage ChildMsg;
    ChildMsg.Value = 303;
    Subsystem->BroadcastMessage(ChildChannel, ChildMsg);

    TestEqual(TEXT("前缀匹配成功接收子通道消息"), Receiver->ReceivedCount, 1);
    TestEqual(TEXT("接收到的具体通道为子通道"), Receiver->LastChannel, ChildChannel);

    ActionToCancel->Cancel();
    TestFalse(TEXT("Cancel 后监听器注销"), Subsystem->HasListener(ParentChannel));

    Subsystem->BroadcastMessage(ChildChannel, ChildMsg);
    TestEqual(TEXT("Cancel 后不再接收子通道广播"), Receiver->ReceivedCount, 1);

    Subsystem->Deinitialize();
    UAsyncAction_ListenForGameplayMessage* ShutDownAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        ParentChannel,
        EGameplayMessageMatch::ExactMatch
    );
    TestNotNull(TEXT("子系统关闭后创建的节点对象有效"), ShutDownAction);
    if (ShutDownAction != nullptr) {
        ShutDownAction->Activate();
    }
    TestEqual(TEXT("子系统关闭态下 Activate 失败自动销毁且监听器为 0"), Subsystem->GetListenerCount(), 0);

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAsyncActionGCSafetyAndBeginDestroyTest,
    "Game.Message.AsyncAction.GCSafetyAndBeginDestroy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FStructOnScope::AddReferencedObjects 对缓存载荷内 UObject 引用的 GC 保活与 BeginDestroy 兜底清理
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FAsyncActionGCSafetyAndBeginDestroyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("AsyncActionGCSafetyTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(World);
    TestNotNull(TEXT("子系统有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.AsyncAction.GCSafety")));

    UAsyncAction_ListenForGameplayMessage* Action = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        Channel,
        EGameplayMessageMatch::ExactMatch
    );
    TestNotNull(TEXT("GC 测试异步节点有效"), Action);
    if (Action == nullptr) {
        GameInstance->Shutdown();
        return false;
    }
    Action->Activate();

    UGameMessageTestReceiver* TransientPayloadObject = NewObject<UGameMessageTestReceiver>(GetTransientPackage());
    TWeakObjectPtr<UGameMessageTestReceiver> WeakPayloadObject(TransientPayloadObject);
    {
        FObjectPayloadTestMessage ObjectMsg;
        ObjectMsg.HeldObject = TransientPayloadObject;
        ObjectMsg.TagId = 808;
        Subsystem->BroadcastMessage(Channel, ObjectMsg);
    }
    TransientPayloadObject = nullptr;

    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("载荷缓存在 AsyncAction 期间，其内部 UObject 必须被 AddReferencedObjects 保活"), WeakPayloadObject.IsValid());

    FObjectPayloadTestMessage ExtractedObjectMsg;
    TestTrue(TEXT("GC 后仍可安全提取含 UObject 引用的载荷"), Action->GetPayloadTyped(ExtractedObjectMsg));
    TestEqual(TEXT("提取出的 TagId 正确"), ExtractedObjectMsg.TagId, 808);
    TestEqual(TEXT("提取出的 HeldObject 指向有效存活对象"), ExtractedObjectMsg.HeldObject.Get(), static_cast<UObject*>(WeakPayloadObject.Get()));
    ExtractedObjectMsg.HeldObject = nullptr;

    Action->Cancel();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("Cancel 清空缓存载荷后，原托管对象应被 GC 正常回收零泄漏"), WeakPayloadObject.IsValid());

    UAsyncAction_ListenForGameplayMessage* UncancelledAction = UAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(
        World,
        Channel,
        EGameplayMessageMatch::ExactMatch
    );
    TestNotNull(TEXT("未显式 Cancel 测试节点有效"), UncancelledAction);
    if (UncancelledAction != nullptr) {
        UncancelledAction->Activate();
        FTestGameplayMessage LeakCheckMsg;
        LeakCheckMsg.Value = 404;
        LeakCheckMsg.Text = TEXT("BeginDestroyCleanup");
        Subsystem->BroadcastMessage(Channel, LeakCheckMsg);
        TestTrue(TEXT("销毁前存在活跃监听器"), Subsystem->HasListener(Channel));

        UncancelledAction->MarkAsGarbage();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        TestFalse(TEXT("未经 Cancel 被 GC 销毁时，BeginDestroy 必须自动注销监听器"), Subsystem->HasListener(Channel));
    }

    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
