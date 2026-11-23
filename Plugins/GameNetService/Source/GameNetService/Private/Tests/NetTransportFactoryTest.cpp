/**
 * @file NetTransportFactoryTest.cpp
 * @brief 网络传输驱动构造工厂（FNetTransportFactory）实例化与自定义工厂注入单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Transport/NetTransportFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetTransportFactoryTest,
    "Game.Net.Service.TransportFactory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证传输工厂能够面向 INetTransport 抽象契约创建单机 Mock 驱动与支持自定义工厂的 WebSocket 驱动
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetTransportFactoryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const TSharedRef<INetTransport> MockTransport = FNetTransportFactory::CreateMockTransport();
    TestEqual(TEXT("工厂创建的 Mock 驱动初始状态为 Disconnected"), MockTransport->GetState(), ENetConnectionState::Disconnected);
    TestTrue(TEXT("工厂创建的 Mock 驱动可直接建立虚拟回环连接"), MockTransport->Connect(TEXT("mock://factory")).IsSuccess());
    TestEqual(TEXT("Mock 驱动连接后状态为 Connected"), MockTransport->GetState(), ENetConnectionState::Connected);

    const TSharedRef<INetTransport> WsTransport = FNetTransportFactory::CreateWebSocketTransport(4096);
    TestEqual(TEXT("工厂创建的 WebSocket 驱动初始状态为 Disconnected"), WsTransport->GetState(), ENetConnectionState::Disconnected);
    TestTrue(TEXT("工厂创建的 WebSocket 驱动拦截非法空地址返回失败态"), WsTransport->Connect(TEXT("")).IsFailure());

    bool bCustomFactoryCalled = false;
    const TSharedRef<INetTransport> CustomWsTransport = FNetTransportFactory::CreateWebSocketTransport(
        2048,
        [&bCustomFactoryCalled](const FString&, const FString&, const TMap<FString, FString>&) -> TSharedPtr<IWebSocket> {
            bCustomFactoryCalled = true;
            return nullptr;
        }
    );
    TestTrue(TEXT("注入返回空指针的自定义工厂时 Connect 安全返回失败态"), CustomWsTransport->Connect(TEXT("wss://stub")).IsFailure());
    TestTrue(TEXT("自定义底层 Socket 工厂被准确透传调用"), bCustomFactoryCalled);

    return true;
}

#endif
