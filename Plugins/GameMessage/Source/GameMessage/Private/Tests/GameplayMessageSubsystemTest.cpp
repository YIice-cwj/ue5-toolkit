/**
 * @file GameplayMessageSubsystemTest.cpp
 * @brief UGameplayMessageSubsystem 核心路由、重入安全、生命周期、错误契约与跨线程自动化测试
 */

#include "Message/GameplayMessageSubsystem.h"

#include "Tests/FTestGameplayMessage.h"
#include "Tests/UGameMessageTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace {

struct FClosureCopyProbe {
    TSharedRef<int32> CopyCount = MakeShared<int32>(0);

    FClosureCopyProbe() = default;

    FClosureCopyProbe(const FClosureCopyProbe& Other)
        : CopyCount(Other.CopyCount) {
        ++(*CopyCount);
    }

    FClosureCopyProbe(FClosureCopyProbe&&) noexcept = default;
    FClosureCopyProbe& operator=(const FClosureCopyProbe&) = default;
    FClosureCopyProbe& operator=(FClosureCopyProbe&&) noexcept = default;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageBroadcastAndListenTest,
    "Game.Message.BroadcastAndListen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证精确匹配、载荷传递与通道隔离
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMessageBroadcastAndListenTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag ChannelHit = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Hit")));
    const FGameplayTag ChannelHeal = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Heal")));

    int32 HitCount = 0;
    int32 LastHitValue = 0;
    FString LastHitText;
    int32 HealCount = 0;

    FGameplayMessageListenerHandle HitHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChannelHit,
        [&HitCount, &LastHitValue, &LastHitText]([[maybe_unused]] FGameplayTag Channel, const FTestGameplayMessage& Payload) {
            ++HitCount;
            LastHitValue = Payload.Value;
            LastHitText = Payload.Text;
        }
    );

    FGameplayMessageListenerHandle HealHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChannelHeal,
        [&HealCount]([[maybe_unused]] FGameplayTag Channel, [[maybe_unused]] const FTestGameplayMessage& Payload) {
            ++HealCount;
        }
    );

    int32 DirectPayloadSeenValue = 0;
    FGameplayMessageListenerHandle PayloadOnlyHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChannelHit,
        [&DirectPayloadSeenValue](const FTestGameplayMessage& Payload) {
            DirectPayloadSeenValue = Payload.Value;
        }
    );

    TestEqual(TEXT("已注册 3 个监听器"), Subsystem->GetListenerCount(), 3);

    FTestGameplayMessage HitPayload;
    HitPayload.Value = 42;
    HitPayload.Text = TEXT("CriticalStrike");
    Subsystem->BroadcastMessage(ChannelHit, HitPayload);

    TestEqual(TEXT("命中通道收到消息"), HitCount, 1);
    TestEqual(TEXT("收到载荷数值一致"), LastHitValue, 42);
    TestEqual(TEXT("仅接收载荷的 Lambda 同样准确捕获数值"), DirectPayloadSeenValue, 42);
    TestEqual(TEXT("收到载荷文本一致"), LastHitText, TEXT("CriticalStrike"));
    TestEqual(TEXT("不同通道互不串扰"), HealCount, 0);

    FTestGameplayMessage HealPayload;
    HealPayload.Value = 100;
    Subsystem->BroadcastMessage(ChannelHeal, HealPayload);
    TestEqual(TEXT("治疗通道收到消息"), HealCount, 1);
    TestEqual(TEXT("命中通道未增加计数"), HitCount, 1);

    HitHandle.Unregister();
    HealHandle.Unregister();
    PayloadOnlyHandle.Unregister();
    TestEqual(TEXT("注销后监听器清空"), Subsystem->GetListenerCount(), 0);

    Subsystem->MarkAsGarbage();
    GameInstance->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessagePartialMatchTest,
    "Game.Message.PartialMatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多层父子标签前缀匹配（零分配树节点遍历）与 HasListener 祖先路由一致性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMessagePartialMatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);

    const FGameplayTag RootChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Combat")));
    const FGameplayTag MidChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Combat.Skill")));
    const FGameplayTag LeafChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Combat.Skill.Damage")));

    int32 RootSeenCount = 0;
    int32 MidExactSeenCount = 0;
    int32 LeafSeenCount = 0;

    FGameplayMessageListenerHandle RootHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        RootChannel,
        [&RootSeenCount]([[maybe_unused]] FGameplayTag Channel, [[maybe_unused]] const FTestGameplayMessage& Payload) {
            ++RootSeenCount;
        },
        EGameplayMessageMatch::PartialMatch
    );

    TestTrue(TEXT("仅父标签有 PartialMatch 监听时，子通道 HasListener 必须与广播路由一致返回 true"), Subsystem->HasListener(LeafChannel));
    TestTrue(TEXT("仅父标签有 PartialMatch 监听时，中间通道 HasListener 返回 true"), Subsystem->HasListener(MidChannel));

    FGameplayMessageListenerHandle MidExactHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        MidChannel,
        [&MidExactSeenCount]([[maybe_unused]] FGameplayTag Channel, [[maybe_unused]] const FTestGameplayMessage& Payload) {
            ++MidExactSeenCount;
        },
        EGameplayMessageMatch::ExactMatch
    );

    FGameplayMessageListenerHandle LeafHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        LeafChannel,
        [&LeafSeenCount]([[maybe_unused]] FGameplayTag Channel, [[maybe_unused]] const FTestGameplayMessage& Payload) {
            ++LeafSeenCount;
        },
        EGameplayMessageMatch::ExactMatch
    );

    FTestGameplayMessage Msg;
    Msg.Value = 10;
    Subsystem->BroadcastMessage(LeafChannel, Msg);

    TestEqual(TEXT("叶子标签广播精确触发叶子监听器"), LeafSeenCount, 1);
    TestEqual(TEXT("中间 ExactMatch 监听器不得拦截子标签广播"), MidExactSeenCount, 0);
    TestEqual(TEXT("根标签 PartialMatch 监听器跨多级捕获叶子广播"), RootSeenCount, 1);

    Subsystem->BroadcastMessage(RootChannel, Msg);
    TestEqual(TEXT("直接向父标签广播时 PartialMatch 监听器仅触发一次（无自身重复捕获）"), RootSeenCount, 2);

    RootHandle.Unregister();
    MidExactHandle.Unregister();
    LeafHandle.Unregister();
    TestFalse(TEXT("全部退订后叶子通道 HasListener 恢复为 false"), Subsystem->HasListener(LeafChannel));

    Subsystem->MarkAsGarbage();
    GameInstance->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageRAIIUnregisterTest,
    "Game.Message.RAIIUnregister",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证句柄作用域离开自动退订、空通道/全重载空回调安全拦截与类型安全过滤
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMessageRAIIUnregisterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.RAII")));

    int32 ReceivedCount = 0;

    {
        FGameplayMessageListenerHandle ScopedHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
            Channel,
            [&ReceivedCount](FGameplayTag, const FTestGameplayMessage&) {
                ++ReceivedCount;
            }
        );
        TestEqual(TEXT("作用域内监听器已存在"), Subsystem->GetListenerCount(), 1);

        FTestGameplayMessage Msg;
        TestTrue(TEXT("作用域内广播成功派发"), Subsystem->BroadcastMessage(Channel, Msg).IsSuccess());
        TestEqual(TEXT("作用域内广播成功送达"), ReceivedCount, 1);

        const FVector WrongPayload(1.0f, 2.0f, 3.0f);
        Subsystem->BroadcastMessage(Channel, WrongPayload);
        TestEqual(TEXT("类型不匹配载荷被安全忽略"), ReceivedCount, 1);
    }

    TestEqual(TEXT("离开作用域后句柄析构自动注销"), Subsystem->GetListenerCount(), 0);

    FTestGameplayMessage AfterMsg;
    Subsystem->BroadcastMessage(Channel, AfterMsg);
    TestEqual(TEXT("退订后广播不再被接收"), ReceivedCount, 1);

    FGameplayMessageListenerHandle InvalidChannelHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        FGameplayTag(),
        [](FGameplayTag, const FTestGameplayMessage&) {}
    );
    TestFalse(TEXT("空通道注册返回无效句柄"), InvalidChannelHandle.IsValid());

    TFunction<void(FGameplayTag, const FTestGameplayMessage&)> EmptyTwoParamCallback;
    FGameplayMessageListenerHandle EmptyTwoParamHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        MoveTemp(EmptyTwoParamCallback)
    );
    TestFalse(TEXT("双参空 TFunction 注册必须被拦截并返回无效句柄"), EmptyTwoParamHandle.IsValid());

    TFunction<void(const FTestGameplayMessage&)> EmptyOneParamCallback;
    FGameplayMessageListenerHandle EmptyOneParamHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        MoveTemp(EmptyOneParamCallback)
    );
    TestFalse(TEXT("单参空 TFunction 注册必须被拦截并返回无效句柄"), EmptyOneParamHandle.IsValid());

    UGameMessageTestReceiver* Receiver = NewObject<UGameMessageTestReceiver>(GameInstance);
    void (UGameMessageTestReceiver::*NullTwoParamMethod)(FGameplayTag, const FTestGameplayMessage&) = nullptr;
    FGameplayMessageListenerHandle NullMethodHandleA = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        Receiver,
        NullTwoParamMethod
    );
    TestFalse(TEXT("双参空成员函数指针必须被拦截"), NullMethodHandleA.IsValid());

    void (UGameMessageTestReceiver::*NullOneParamMethod)(const FTestGameplayMessage&) = nullptr;
    FGameplayMessageListenerHandle NullMethodHandleB = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        Receiver,
        NullOneParamMethod
    );
    TestFalse(TEXT("单参空成员函数指针必须被拦截"), NullMethodHandleB.IsValid());

    TestEqual(TEXT("全部非法注册均未污染监听器计数"), Subsystem->GetListenerCount(), 0);
    TestTrue(TEXT("空通道广播返回 Failure"), Subsystem->BroadcastMessage(FGameplayTag(), AfterMsg).IsFailure());
    TestTrue(TEXT("空通道入队返回 Failure"), Subsystem->EnqueueMessage(FGameplayTag(), AfterMsg).IsFailure());

    Receiver->MarkAsGarbage();
    Subsystem->MarkAsGarbage();
    GameInstance->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageHasListenerAndStatusResultTest,
    "Game.Message.HasListenerAndStatusResult",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 HasListener 状态判定与 Game::ModuleId::Message 结构化错误契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMessageHasListenerAndStatusResultTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.StatusResult")));

    TestFalse(TEXT("空标签 HasListener 返回 false"), Subsystem->HasListener(FGameplayTag()));
    TestFalse(TEXT("初始状态 HasListener 为 false"), Subsystem->HasListener(Channel));

    FGameplayMessageListenerHandle Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        []([[maybe_unused]] FGameplayTag, [[maybe_unused]] const FTestGameplayMessage&) {}
    );
    TestTrue(TEXT("注册后 HasListener 为 true"), Subsystem->HasListener(Channel));

    FTestGameplayMessage Msg;
    const TGameStatusResult<void> InvalidTagBroadcast = Subsystem->BroadcastMessage(FGameplayTag(), Msg);
    TestTrue(TEXT("无效标签广播失败"), InvalidTagBroadcast.IsFailure());
    TestEqual(TEXT("无效标签广播 ModuleId 为 Game::ModuleId::Message"), InvalidTagBroadcast.GetError().ModuleId, Game::ModuleId::Message);
    TestEqual(TEXT("无效标签广播错误码一致"), InvalidTagBroadcast.GetError().ErrorCode, Game::MessageErrorCodes::InvalidChannel);

    const TGameStatusResult<void> InvalidTagEnqueue = Subsystem->EnqueueMessage(FGameplayTag(), Msg);
    TestTrue(TEXT("无效标签入队失败"), InvalidTagEnqueue.IsFailure());
    TestEqual(TEXT("无效标签入队 ModuleId 为 Game::ModuleId::Message"), InvalidTagEnqueue.GetError().ModuleId, Game::ModuleId::Message);
    TestEqual(TEXT("无效标签入队错误码一致"), InvalidTagEnqueue.GetError().ErrorCode, Game::MessageErrorCodes::InvalidChannel);

    Handle.Unregister();
    TestFalse(TEXT("注销后 HasListener 恢复为 false"), Subsystem->HasListener(Channel));

    Subsystem->Deinitialize();
    const TGameStatusResult<void> ShuttingDownEnqueue = Subsystem->EnqueueMessage(Channel, Msg);
    TestTrue(TEXT("关闭态入队失败"), ShuttingDownEnqueue.IsFailure());
    TestEqual(TEXT("关闭态入队 ModuleId 为 Game::ModuleId::Message"), ShuttingDownEnqueue.GetError().ModuleId, Game::ModuleId::Message);
    TestEqual(TEXT("关闭态入队错误码一致"), ShuttingDownEnqueue.GetError().ErrorCode, Game::MessageErrorCodes::SubsystemShuttingDown);

    Subsystem->MarkAsGarbage();
    GameInstance->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageReentrancyAndPolymorphismTest,
    "Game.Message.ReentrancyAndPolymorphism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证广播期重入注销他方监听器静默守卫、重入注册、嵌套广播、USTRUCT 继承多态与死 UObject 自动跳过
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMessageReentrancyAndPolymorphismTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
    TStrongObjectPtr<UGameplayMessageSubsystem> Subsystem(NewObject<UGameplayMessageSubsystem>(GameInstance.Get()));
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Reentrancy")));
    const FGameplayTag NestedChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Reentrancy.Nested")));

    int32 ListenerACount = 0;
    int32 ListenerBCount = 0;
    int32 DynamicListenerCCount = 0;
    int32 NestedSeenCount = 0;

    FGameplayMessageListenerHandle HandleB;
    FGameplayMessageListenerHandle DynamicHandleC;

    FGameplayMessageListenerHandle HandleA = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [&](FGameplayTag, const FTestGameplayMessage& Payload) {
            ++ListenerACount;
            HandleB.Unregister();
            if (!DynamicHandleC.IsValid()) {
                DynamicHandleC = Subsystem->RegisterListener<FTestGameplayMessage>(
                    Channel,
                    [&DynamicListenerCCount](const FTestGameplayMessage&) {
                        ++DynamicListenerCCount;
                    }
                );
            }
            if (Payload.Value == 1) {
                FTestGameplayMessage InnerMsg;
                InnerMsg.Value = 99;
                Subsystem->BroadcastMessage(NestedChannel, InnerMsg);
            }
        }
    );

    HandleB = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [&ListenerBCount](const FTestGameplayMessage&) {
            ++ListenerBCount;
        }
    );

    FGameplayMessageListenerHandle NestedHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        NestedChannel,
        [&NestedSeenCount](const FTestGameplayMessage& Payload) {
            if (Payload.Value == 99) {
                ++NestedSeenCount;
            }
        }
    );

    FTestGameplayMessage Msg1;
    Msg1.Value = 1;
    Subsystem->BroadcastMessage(Channel, Msg1);
    TestEqual(TEXT("监听器 A 执行一次"), ListenerACount, 1);
    TestEqual(TEXT("同轮中被 A 注销的监听器 B 绝不得再被回调"), ListenerBCount, 0);
    TestEqual(TEXT("同轮中动态注册的监听器 C 不在当轮触发"), DynamicListenerCCount, 0);
    TestEqual(TEXT("回调内嵌套广播正常送达"), NestedSeenCount, 1);

    FTestGameplayMessage Msg2;
    Msg2.Value = 2;
    Subsystem->BroadcastMessage(Channel, Msg2);
    TestEqual(TEXT("第二轮动态监听器 C 正常生效"), DynamicListenerCCount, 1);
    TestEqual(TEXT("已注销的监听器 B 持续保持静默"), ListenerBCount, 0);

    HandleA.Unregister();
    DynamicHandleC.Unregister();
    NestedHandle.Unregister();

    int32 BaseSeenValue = 0;
    FString BaseSeenText;
    FGameplayMessageListenerHandle BaseHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [&BaseSeenValue, &BaseSeenText](const FTestGameplayMessage& BasePayload) {
            BaseSeenValue = BasePayload.Value;
            BaseSeenText = BasePayload.Text;
        }
    );

    FDerivedTestGameplayMessage DerivedMsg;
    DerivedMsg.Value = 777;
    DerivedMsg.Text = TEXT("DerivedPayload");
    DerivedMsg.ExtraCode = 999;
    TestTrue(TEXT("广播派生结构体成功"), Subsystem->BroadcastMessage(Channel, DerivedMsg).IsSuccess());
    TestEqual(TEXT("基类监听器成功按 IsChildOf 接收派生载荷 Value"), BaseSeenValue, 777);
    TestEqual(TEXT("基类监听器成功按 IsChildOf 接收派生载荷 Text"), BaseSeenText, TEXT("DerivedPayload"));
    BaseHandle.Unregister();

    UGameMessageTestReceiver* EphemeralReceiver = NewObject<UGameMessageTestReceiver>(GetTransientPackage());
    FGameplayMessageListenerHandle DeadOwnerHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        EphemeralReceiver,
        &UGameMessageTestReceiver::OnPayloadOnlyReceived
    );
    EphemeralReceiver->MarkAsGarbage();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("宿主被 GC 回收后广播安全跳过不崩溃"), Subsystem->BroadcastMessage(Channel, Msg2).IsSuccess());
    DeadOwnerHandle.Unregister();

    Subsystem->MarkAsGarbage();
    GameInstance->MarkAsGarbage();
    Subsystem.Reset();
    GameInstance.Reset();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageLifecycleDeinitAndReinitTest,
    "Game.Message.LifecycleDeinitAndReinit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Deinitialize 丢弃队列与纪元作废、Re-Initialize 恢复能力、跨纪元 ABA 防护与 GetPtr 上下文边界
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMessageLifecycleDeinitAndReinitTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestNull(TEXT("GetPtr(nullptr) 安全返回 nullptr"), UGameplayMessageSubsystem::GetPtr(nullptr));

    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameplayMessageLifecycleTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(World);
    TestNotNull(TEXT("通过 World 获取子系统有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    TestEqual(TEXT("Get(World) 返回同一子系统实例"), &UGameplayMessageSubsystem::Get(World), Subsystem);
    TestEqual(TEXT("通过同类 GameInstanceSubsystem 直通获取子系统"), UGameplayMessageSubsystem::GetPtr(Subsystem), Subsystem);

    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Lifecycle")));
    int32 Epoch1Count = 0;
    FGameplayMessageListenerHandle Epoch1Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [&Epoch1Count](const FTestGameplayMessage&) {
            ++Epoch1Count;
        }
    );

    FTestGameplayMessage QueuedBeforeShutdown;
    QueuedBeforeShutdown.Value = 11;
    TestTrue(TEXT("关闭前 EnqueueMessage 成功"), Subsystem->EnqueueMessage(Channel, QueuedBeforeShutdown).IsSuccess());

    Subsystem->Deinitialize();
    TestFalse(TEXT("Deinitialize 后旧纪元句柄即刻失效"), Epoch1Handle.IsValid());
    TestEqual(TEXT("Deinitialize 后监听器计数归零"), Subsystem->GetListenerCount(), 0);
    TestEqual(TEXT("Deinitialize 后 FlushPendingMessages 排空 0 条（残留任务已丢弃）"), Subsystem->FlushPendingMessages(), 0);
    TestEqual(TEXT("Deinitialize 丢弃的残留任务未触发旧回调"), Epoch1Count, 0);

    FGameplayMessageListenerHandle ShutDownRegisterHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [](const FTestGameplayMessage&) {}
    );
    TestFalse(TEXT("关闭态拒绝新注册监听器并返回无效句柄"), ShutDownRegisterHandle.IsValid());
    TestEqual(TEXT("关闭态拒绝注册后监听器计数仍为 0"), Subsystem->GetListenerCount(), 0);

    FObjectSubsystemCollection<UGameInstanceSubsystem> ReinitCollection;
    Subsystem->Initialize(ReinitCollection);
    int32 Epoch2Count = 0;
    FGameplayMessageListenerHandle Epoch2Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [&Epoch2Count](const FTestGameplayMessage&) {
            ++Epoch2Count;
        }
    );
    TestTrue(TEXT("Re-Initialize 后新纪元句柄有效"), Epoch2Handle.IsValid());

    Epoch1Handle.Unregister();
    TestTrue(TEXT("销毁旧纪元句柄绝不误伤新纪元同 ID 监听器（防跨纪元 ABA）"), Epoch2Handle.IsValid());
    TestEqual(TEXT("新纪元监听器计数保持 1"), Subsystem->GetListenerCount(), 1);

    FTestGameplayMessage Epoch2Msg;
    Epoch2Msg.Value = 22;
    TestTrue(TEXT("Re-Initialize 后 EnqueueMessage 恢复正常"), Subsystem->EnqueueMessage(Channel, Epoch2Msg).IsSuccess());
    TestEqual(TEXT("Re-Initialize 后主线程排空 1 条消息"), Subsystem->FlushPendingMessages(), 1);
    TestEqual(TEXT("新纪元回调准确触发 1 次"), Epoch2Count, 1);

    Epoch2Handle.Unregister();
    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageCrossThreadAndZeroCopyTest,
    "Game.Message.CrossThreadEnqueueAndZeroCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发 EnqueueMessage 主线程串行排空、广播热路径闭包零拷贝及回调内自退订安全
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameMessageCrossThreadAndZeroCopyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameplayMessageCrossThreadTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* Subsystem = UGameplayMessageSubsystem::GetPtr(World);
    TestNotNull(TEXT("消息子系统实例有效"), Subsystem);
    if (Subsystem == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    const FGameplayTag AsyncChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(
        FName(TEXT("UnitTest.Message.CrossThread"))
    );

    int32 DeliveredCount = 0;
    int64 DeliveredSum = 0;
    FClosureCopyProbe Probe;
    const TSharedRef<int32> CopyCounter = Probe.CopyCount;

    FGameplayMessageListenerHandle Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
        AsyncChannel,
        [&DeliveredCount, &DeliveredSum, CapturedProbe = MoveTemp(Probe)](
            [[maybe_unused]] FGameplayTag Channel,
            const FTestGameplayMessage& Payload
        ) {
            if (*CapturedProbe.CopyCount >= 0) {
                ++DeliveredCount;
                DeliveredSum += Payload.Value;
            }
        }
    );

    const int32 CopiesAfterRegister = *CopyCounter;
    for (int32 Index = 1; Index <= 20; ++Index) {
        FTestGameplayMessage SyncMsg;
        SyncMsg.Value = Index;
        Subsystem->BroadcastMessage(AsyncChannel, SyncMsg);
    }
    TestEqual(TEXT("高频广播热路径期间监听器闭包发生 0 次拷贝"), *CopyCounter, CopiesAfterRegister);
    TestEqual(TEXT("同步广播 20 次计数准确"), DeliveredCount, 20);

    DeliveredCount = 0;
    DeliveredSum = 0;
    constexpr int32 WorkerCount = 8;
    constexpr int32 MessagesPerWorker = 25;

    ParallelFor(WorkerCount, [Subsystem, AsyncChannel](int32 WorkerIdx) {
        for (int32 Step = 1; Step <= MessagesPerWorker; ++Step) {
            FTestGameplayMessage QueuedMsg;
            QueuedMsg.Value = WorkerIdx * 100 + Step;
            Subsystem->EnqueueMessage(AsyncChannel, MoveTemp(QueuedMsg));
        }
    });

    const int32 FlushedCount = Subsystem->FlushPendingMessages();
    TestTrue(TEXT("主线程显式排空处理的跨线程消息数大于 0"), FlushedCount > 0);
    TestEqual(TEXT("8 个工作线程共 200 条跨线程消息零丢失送达"), DeliveredCount, WorkerCount * MessagesPerWorker);

    int64 ExpectedSum = 0;
    for (int32 WorkerIdx = 0; WorkerIdx < WorkerCount; ++WorkerIdx) {
        for (int32 Step = 1; Step <= MessagesPerWorker; ++Step) {
            ExpectedSum += WorkerIdx * 100 + Step;
        }
    }
    TestEqual(TEXT("跨线程消息载荷累加和精确一致"), DeliveredSum, ExpectedSum);

    int32 SelfRemoveTriggered = 0;
    FGameplayMessageListenerHandle SelfRemoveHandle;
    SelfRemoveHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        AsyncChannel,
        [&SelfRemoveTriggered, &SelfRemoveHandle](FGameplayTag, const FTestGameplayMessage&) {
            ++SelfRemoveTriggered;
            SelfRemoveHandle.Unregister();
        }
    );
    FTestGameplayMessage TriggerMsg;
    TriggerMsg.Value = 1;
    Subsystem->BroadcastMessage(AsyncChannel, TriggerMsg);
    Subsystem->BroadcastMessage(AsyncChannel, TriggerMsg);
    TestEqual(TEXT("广播回调内自注销安全且仅触发一次"), SelfRemoveTriggered, 1);

    Handle.Unregister();
    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageMemberListenerAndDirectContextTest,
    "Game.Message.MemberListenerAndDirectContext",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GameInstance 直通获取、成员函数监听器注册以及空指针安全拦截
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameMessageMemberListenerAndDirectContextTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameplayMessageMemberTestWorld")));

    UGameplayMessageSubsystem* Subsystem = GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("子系统实例有效"), Subsystem);
    if (!Subsystem) {
        GameInstance->Shutdown();
        return false;
    }

    UGameplayMessageSubsystem* DirectSubsystem = UGameplayMessageSubsystem::GetPtr(GameInstance);
    TestEqual(TEXT("直接以 GameInstance 作为上下文可正确获取子系统"), DirectSubsystem, Subsystem);

    const int32 InitialListenerCount = Subsystem->GetListenerCount();

    const FGameplayTag ChannelA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Member.A")));
    const FGameplayTag ChannelB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Member.B")));

    UGameMessageTestReceiver* Receiver = NewObject<UGameMessageTestReceiver>(GameInstance);

    FGameplayMessageListenerHandle HandleA = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChannelA,
        Receiver,
        &UGameMessageTestReceiver::OnTypedMessageReceived
    );
    TestTrue(TEXT("成员函数监听器 A 注册有效"), HandleA.IsValid());

    FGameplayMessageListenerHandle HandleB = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChannelB,
        Receiver,
        &UGameMessageTestReceiver::OnPayloadOnlyReceived
    );
    TestTrue(TEXT("成员函数监听器 B 注册有效"), HandleB.IsValid());
    TestEqual(TEXT("注册 2 个成员监听器后计数累加 2"), Subsystem->GetListenerCount(), InitialListenerCount + 2);

    FTestGameplayMessage MsgA;
    MsgA.Value = 100;
    Subsystem->BroadcastMessage(ChannelA, MsgA);
    TestEqual(TEXT("成员函数 A 收到广播通道正确"), Receiver->LastChannel, ChannelA);
    TestEqual(TEXT("成员函数 A 收到载荷数值正确"), Receiver->LastPayloadValue, 100);
    TestEqual(TEXT("累计接收计数为 1"), Receiver->ReceivedCount, 1);

    FTestGameplayMessage MsgB;
    MsgB.Value = 200;
    Subsystem->BroadcastMessage(ChannelB, MsgB);
    TestEqual(TEXT("成员函数 B 收到载荷数值正确"), Receiver->LastPayloadValue, 200);
    TestEqual(TEXT("累计接收计数为 2"), Receiver->ReceivedCount, 2);

    UGameMessageTestReceiver* NullReceiver = nullptr;
    FGameplayMessageListenerHandle NullHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        ChannelA,
        NullReceiver,
        &UGameMessageTestReceiver::OnTypedMessageReceived
    );
    TestFalse(TEXT("空宿主注册返回无效句柄"), NullHandle.IsValid());

    HandleA.Unregister();
    HandleB.Unregister();
    TestEqual(TEXT("全部手动注销后恢复初始监听器数量"), Subsystem->GetListenerCount(), InitialListenerCount);

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageCrossThreadHandleLifecycleTest,
    "Game.Message.CrossThreadHandleLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证工作线程异步销毁句柄时安全跳回 GameThread 注销且无断言崩溃
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameMessageCrossThreadHandleLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.CrossThreadHandle")));

    int32 ReceivedCount = 0;

    {
        FGameplayMessageListenerHandle Handle = Subsystem->RegisterListener<FTestGameplayMessage>(
            Channel,
            [&ReceivedCount](FGameplayTag, const FTestGameplayMessage&) {
                ++ReceivedCount;
            }
        );
        TestTrue(TEXT("监听句柄有效"), Handle.IsValid());
        TestEqual(TEXT("子系统当前监听器数量为 1"), Subsystem->GetListenerCount(), 1);

        TFuture<void> WorkerFuture = Async(EAsyncExecution::Thread, [MovedHandle = MoveTemp(Handle)]() mutable {
            MovedHandle.Unregister();
        });
        WorkerFuture.Wait();
    }

    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

    TestEqual(TEXT("跨线程安全退订后子系统监听器清空"), Subsystem->GetListenerCount(), 0);

    Subsystem->MarkAsGarbage();
    GameInstance->MarkAsGarbage();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
