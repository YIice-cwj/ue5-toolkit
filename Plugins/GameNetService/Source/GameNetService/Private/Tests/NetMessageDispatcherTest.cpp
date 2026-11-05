/**
 * @file NetMessageDispatcherTest.cpp
 * @brief 常驻协议多播监听器注册、注销与回调内自注销快照安全路由器（FNetMessageDispatcher）单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dispatch/NetMessageDispatcher.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetMessageDispatcherTest,
    "Game.Net.Service.MessageDispatcher",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证常驻处理器注册注销、快照迭代安全及清空行为
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetMessageDispatcherTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetMessageDispatcher Dispatcher;

    TestEqual(TEXT("注册空处理器返回无效句柄 0"), Dispatcher.RegisterHandler(100, nullptr), int64(0));
    TestFalse(TEXT("注销不存在协议的句柄返回 false"), Dispatcher.UnregisterHandler(100, 99));

    int32 BroadcastHitCount = 0;
    int64 SelfUnregisterHandle = 0;
    SelfUnregisterHandle = Dispatcher.RegisterHandler(
        500,
        [&Dispatcher, &BroadcastHitCount, &SelfUnregisterHandle](const FNetPacketHeader&, TConstArrayView<uint8>) {
            ++BroadcastHitCount;
            Dispatcher.UnregisterHandler(500, SelfUnregisterHandle);
        }
    );
    const int64 SecondHandle = Dispatcher.RegisterHandler(
        500,
        [&BroadcastHitCount](const FNetPacketHeader&, TConstArrayView<uint8> Payload) {
            BroadcastHitCount += Payload.IsEmpty() ? 10 : Payload[0];
        }
    );
    TestEqual(TEXT("协议 500 挂载了 2 个常驻处理器"), Dispatcher.GetHandlerCount(500), 2);

    FNetPacketHeader PushHeader;
    PushHeader.MessageId = 500;
    TestTrue(TEXT("派发协议 500 命中常驻处理器"), Dispatcher.DispatchPacket(PushHeader, {}));
    TestEqual(TEXT("回调内自注销不破坏快照迭代，两个处理器均被执行"), BroadcastHitCount, 11);
    TestEqual(TEXT("自注销后剩余 1 个处理器"), Dispatcher.GetHandlerCount(500), 1);

    TestFalse(TEXT("重复注销已移除句柄返回 false"), Dispatcher.UnregisterHandler(500, SelfUnregisterHandle));
    TestTrue(TEXT("注销第二个处理器返回 true"), Dispatcher.UnregisterHandler(500, SecondHandle));
    TestEqual(TEXT("全部移除后处理器计数为 0"), Dispatcher.GetHandlerCount(500), 0);
    TestFalse(TEXT("无监听器的报文派发返回 false"), Dispatcher.DispatchPacket(PushHeader, {}));

    const int64 TempHandle = Dispatcher.RegisterHandler(
        600,
        [](const FNetPacketHeader&, TConstArrayView<uint8>) {}
    );
    TestTrue(TEXT("新协议注册返回有效句柄"), TempHandle > 0);
    Dispatcher.ClearHandlers();
    TestEqual(TEXT("ClearHandlers 后全部监听器清空"), Dispatcher.GetHandlerCount(600), 0);

    return true;
}

#endif

