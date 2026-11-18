// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameplayMessageTypesTest.cpp
 * @brief 消息系统基础类型与句柄契约自动化测试
 */

#include "Message/GameplayMessageTypes.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Tests/GameMessageTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageTypesHandleLifecycleTest,
    "Game.Message.Types.HandleLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameplayMessageListenerHandle 默认构造、移动语义、生命周期引用计数与退订幂等性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMessageTypesHandleLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameplayMessageListenerHandle DefaultHandle;
    TestFalse(TEXT("默认构造句柄为无效状态"), DefaultHandle.IsValid());

    DefaultHandle.Unregister();
    TestFalse(TEXT("对无效句柄调用 Unregister 为安全空操作"), DefaultHandle.IsValid());

    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UGameplayMessageSubsystem* Subsystem = NewObject<UGameplayMessageSubsystem>(GameInstance);
    const FGameplayTag Channel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("UnitTest.Message.Types.Handle")));

    int32 CallbackCount = 0;
    FGameplayMessageListenerHandle ActiveHandle = Subsystem->RegisterListener<FTestGameplayMessage>(
        Channel,
        [&CallbackCount]([[maybe_unused]] FGameplayTag InChannel, [[maybe_unused]] const FTestGameplayMessage& Payload) {
            ++CallbackCount;
        }
    );

    TestTrue(TEXT("注册有效通道返回有效句柄"), ActiveHandle.IsValid());
    TestEqual(TEXT("子系统当前监听器数量为 1"), Subsystem->GetListenerCount(), 1);

    FGameplayMessageListenerHandle CopiedHandle = ActiveHandle;
    TestTrue(TEXT("拷贝构造句柄共享有效性"), CopiedHandle.IsValid());
    TestTrue(TEXT("原句柄依然有效"), ActiveHandle.IsValid());
    TestTrue(TEXT("同源句柄比较相等"), ActiveHandle == CopiedHandle);
    TestFalse(TEXT("同源句柄不等比较返回 false"), ActiveHandle != CopiedHandle);
    TestEqual(TEXT("同源句柄哈希值完全一致"), GetTypeHash(ActiveHandle), GetTypeHash(CopiedHandle));
    TestEqual(TEXT("拷贝句柄不增加子系统监听器数量"), Subsystem->GetListenerCount(), 1);

    FGameplayMessageListenerHandle OtherHandle;
    TestTrue(TEXT("不同句柄比较不相等"), ActiveHandle != OtherHandle);
    TestFalse(TEXT("不同句柄相等比较返回 false"), ActiveHandle == OtherHandle);

    FGameplayMessageListenerHandle MovedHandle = MoveTemp(ActiveHandle);
    TestTrue(TEXT("移动构造后目标句柄接管有效性"), MovedHandle.IsValid());
    TestFalse(TEXT("移动后源句柄置为空状态"), ActiveHandle.IsValid());

    FGameplayMessageListenerHandle AssignedHandle;
    AssignedHandle = MoveTemp(MovedHandle);
    TestTrue(TEXT("移动赋值后目标句柄有效"), AssignedHandle.IsValid());
    TestFalse(TEXT("移动赋值后源句柄置为空状态"), MovedHandle.IsValid());

    AssignedHandle.Unregister();
    TestFalse(TEXT("显式退订后句柄失效"), AssignedHandle.IsValid());
    TestFalse(TEXT("同源拷贝句柄同步失效"), CopiedHandle.IsValid());
    TestEqual(TEXT("显式注销后子系统监听器已移除"), Subsystem->GetListenerCount(), 0);

    AssignedHandle.Unregister();
    TestEqual(TEXT("重复注销保持幂等性"), Subsystem->GetListenerCount(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMessageTypesEnumContractTest,
    "Game.Message.Types.EnumContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 EGameplayMessageMatch 规则枚举完整性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMessageTypesEnumContractTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const EGameplayMessageMatch Exact = EGameplayMessageMatch::ExactMatch;
    const EGameplayMessageMatch Partial = EGameplayMessageMatch::PartialMatch;

    TestNotEqual(TEXT("ExactMatch 与 PartialMatch 枚举值不同"), static_cast<uint8>(Exact), static_cast<uint8>(Partial));
    TestEqual(TEXT("ExactMatch 枚举底层序数为 0"), static_cast<uint8>(Exact), static_cast<uint8>(0));
    TestEqual(TEXT("PartialMatch 枚举底层序数为 1"), static_cast<uint8>(Partial), static_cast<uint8>(1));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
