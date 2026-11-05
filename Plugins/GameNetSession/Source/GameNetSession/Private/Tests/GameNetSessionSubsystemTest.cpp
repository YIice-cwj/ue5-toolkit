/**
 * @file GameNetSessionSubsystemTest.cpp
 * @brief 房间会话门面子系统（UGameNetSessionSubsystem）生命周期、跨实例局域网发现编排与状态机测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "System/GameNetSessionSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetSessionSubsystemTest,
    "Game.Net.Session.Subsystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证会话门面子系统跨世界实例局域网发现编排、状态机切换与无 OSS 安全降级委托
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetSessionSubsystemTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* HostGI = NewObject<UGameInstance>(GEngine);
    HostGI->InitializeStandalone(FName(TEXT("GameNetSessionHostTestWorld")));

    UGameInstance* ClientGI = NewObject<UGameInstance>(GEngine);
    ClientGI->InitializeStandalone(FName(TEXT("GameNetSessionClientTestWorld")));

    UGameNetSessionSubsystem& HostSession = UGameNetSessionSubsystem::Get(HostGI);
    UGameNetSessionSubsystem& ClientSession = UGameNetSessionSubsystem::Get(ClientGI->GetWorld());
    TestNull(TEXT("空对象查询会话子系统返回 nullptr"), UGameNetSessionSubsystem::GetPtr(nullptr));
    TestEqual(TEXT("初始会话状态为 Idle"), HostSession.GetSessionState(), EGameNetSessionState::Idle);

    FGameNetRoomDescriptor DummyRoom;
    DummyRoom.HostAddress = TEXT("192.168.1.25");
    DummyRoom.HostGamePort = 8888;
    TestFalse(TEXT("无本地 PlayerController 时 TravelToRoom 安全返回 false"), ClientSession.TravelToRoom(DummyRoom));
    TestFalse(TEXT("非法地图路径 TravelToListenServer 安全返回 false"), HostSession.TravelToListenServer(TEXT(""), 7777));

    FGameNetCreateRoomRequest BadReq;
    BadReq.RoomName.Empty();
    TestFalse(TEXT("CreateOnlineRoom 非法参数安全返回 false"), HostSession.CreateOnlineRoom(BadReq, 0));
    TestFalse(TEXT("FindOnlineRooms 非法条数安全返回 false"), ClientSession.FindOnlineRooms(true, 0, 0));
    TestFalse(TEXT("JoinOnlineRoomByIndex 越界下标安全返回 false"), ClientSession.JoinOnlineRoomByIndex(0, 0));
    TestFalse(TEXT("DestroyOnlineRoom 无活跃命名会话安全返回 false"), HostSession.DestroyOnlineRoom());
    TestEqual(TEXT("无在线搜索结果时返回空数组"), ClientSession.GetOnlineSearchResults().Num(), 0);

    FGameNetCreateRoomRequest LanReq;
    LanReq.RoomName = TEXT("SubsystemLanRoom");
    LanReq.MapPath = TEXT("/Engine/Maps/Entry");
    LanReq.HostGamePort = 18888;
    LanReq.MaxPlayers = 6;

    TestTrue(TEXT("子系统启动局域网房间广播成功"), HostSession.HostLanRoom(LanReq, 0, TEXT("127.0.0.1")));
    TestEqual(TEXT("房主会话状态切换至 Hosting"), HostSession.GetSessionState(), EGameNetSessionState::Hosting);

    TestTrue(TEXT("子系统启动局域网发现成功"), ClientSession.StartLanDiscovery(0, TEXT("127.0.0.1")));
    TestEqual(TEXT("客户端会话状态切换至 Searching"), ClientSession.GetSessionState(), EGameNetSessionState::Searching);

    TestTrue(
        TEXT("客户端子系统发出探测包成功"),
        ClientSession.SendLanDiscoveryProbe(TEXT("127.0.0.1"), HostSession.GetLanBeaconBoundPort())
    );

    TestTrue(TEXT("房主信标收到探测"), HostSession.GetLanBeacon().WaitForIncomingData(1.0));
    HostSession.Tick(0.01f);

    TestTrue(TEXT("客户端信标收到房间宣告"), ClientSession.GetLanBeacon().WaitForIncomingData(1.0));
    ClientSession.Tick(0.02f);

    const TArray<FGameNetRoomDescriptor> Rooms = ClientSession.GetDiscoveredLanRooms();
    TestEqual(TEXT("客户端子系统查询到 1 个局域网房间"), Rooms.Num(), 1);
    if (Rooms.Num() == 1) {
        TestEqual(TEXT("子系统房间名一致"), Rooms[0].RoomName, TEXT("SubsystemLanRoom"));
        TestEqual(TEXT("子系统最大人数一致"), Rooms[0].MaxPlayers, 6);
    }

    HostSession.StopLanSession();
    ClientSession.StopLanSession();
    TestEqual(TEXT("停止后房主会话状态归位 Idle"), HostSession.GetSessionState(), EGameNetSessionState::Idle);
    TestEqual(TEXT("停止后客户端会话状态归位 Idle"), ClientSession.GetSessionState(), EGameNetSessionState::Idle);

    HostGI->Shutdown();
    ClientGI->Shutdown();
    return true;
}

#endif

