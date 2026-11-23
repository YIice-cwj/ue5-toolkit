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
 * @brief 验证会话门面子系统 GetPtr 多上下文支持、跨实例局域网发现与动态更新编排及无 OSS 安全降级
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
    TestEqual(
        TEXT("通过 UGameInstanceSubsystem 指针也能正确反查同实例会话子系统"),
        UGameNetSessionSubsystem::GetPtr(&HostSession),
        &HostSession
    );
    TestEqual(TEXT("初始会话状态为 Idle"), HostSession.GetSessionState(), EGameNetSessionState::Idle);

    FGameNetRoomDescriptor DummyRoom;
    DummyRoom.HostAddress = TEXT("192.168.1.25");
    DummyRoom.HostGamePort = 8888;
    TestFalse(TEXT("无本地 PlayerController 时 TravelToRoom 安全返回 false"), ClientSession.TravelToRoom(DummyRoom));
    TestFalse(TEXT("非法地图路径 TravelToListenServer 安全返回 false"), HostSession.TravelToListenServer(TEXT(""), 7777));

    FGameNetCreateRoomRequest BadReq;
    BadReq.RoomName.Empty();
    TestFalse(TEXT("CreateOnlineRoom 非法参数安全返回 false"), HostSession.CreateOnlineRoom(BadReq, 0));
    TestEqual(TEXT("CreateOnlineRoom 失败后状态安全回滚为 Idle"), HostSession.GetSessionState(), EGameNetSessionState::Idle);
    TestFalse(TEXT("FindOnlineRooms 非法条数安全返回 false"), ClientSession.FindOnlineRooms(true, 0, 0));
    TestEqual(TEXT("FindOnlineRooms 失败后状态安全回滚为 Idle"), ClientSession.GetSessionState(), EGameNetSessionState::Idle);
    TestFalse(TEXT("JoinOnlineRoomByIndex 越界下标安全返回 false"), ClientSession.JoinOnlineRoomByIndex(0, 0));
    TestFalse(TEXT("DestroyOnlineRoom 无活跃命名会话安全返回 false"), HostSession.DestroyOnlineRoom());
    TestEqual(TEXT("无在线搜索结果时返回空数组"), ClientSession.GetOnlineSearchResults().Num(), 0);
    TestEqual(TEXT("无在线搜索结果时返回空引用"), ClientSession.GetOnlineSearchResultsRef().Num(), 0);

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

    TestTrue(TEXT("房主信标收到探测"), HostSession.WaitForLanBeaconData(1.0));
    HostSession.Tick(0.01f);

    TestTrue(TEXT("客户端信标收到房间宣告"), ClientSession.WaitForLanBeaconData(1.0));
    ClientSession.Tick(0.02f);

    const TArray<FGameNetRoomDescriptor> Rooms = ClientSession.GetDiscoveredLanRooms();
    TestEqual(TEXT("客户端子系统查询到 1 个局域网房间"), Rooms.Num(), 1);
    TArray<FGameNetRoomDescriptor> ReusableRooms;
    ClientSession.GetDiscoveredLanRoomsTo(ReusableRooms);
    TestEqual(TEXT("客户端子系统复用接口查询到 1 个局域网房间"), ReusableRooms.Num(), 1);
    if (Rooms.Num() == 1) {
        TestEqual(TEXT("子系统房间名一致"), Rooms[0].RoomName, TEXT("SubsystemLanRoom"));
        TestEqual(TEXT("子系统最大人数一致"), Rooms[0].MaxPlayers, 6);

        FGameNetRoomDescriptor UpdatedRoom = Rooms[0];
        UpdatedRoom.CurrentPlayers = 4;
        TestTrue(TEXT("子系统合法更新房主信息返回 true"), HostSession.UpdateLanHostedRoom(UpdatedRoom));

        FGameNetRoomDescriptor InvalidUpdatedRoom = UpdatedRoom;
        InvalidUpdatedRoom.CurrentPlayers = 99;
        TestFalse(TEXT("子系统人数超限更新房主信息被安全拒绝"), HostSession.UpdateLanHostedRoom(InvalidUpdatedRoom));

        TestFalse(TEXT("Hosting 状态下发起非法在线建房安全返回 false"), HostSession.CreateOnlineRoom(BadReq, 0));
        TestEqual(TEXT("非法在线建房拒绝后仍保持 Hosting 状态不破坏"), HostSession.GetSessionState(), EGameNetSessionState::Hosting);

        TestTrue(
            TEXT("房主子系统主动单播刷新宣告成功"),
            HostSession.BroadcastLanAnnouncement(TEXT("127.0.0.1"), ClientSession.GetLanBeaconBoundPort())
        );
        TestTrue(TEXT("客户端信标收到更新后的宣告"), ClientSession.WaitForLanBeaconData(1.0));
        ClientSession.Tick(0.01f);
        ClientSession.GetDiscoveredLanRoomsTo(ReusableRooms);
        TestEqual(TEXT("更新宣告后房间数保持 1"), ReusableRooms.Num(), 1);
        TestEqual(TEXT("客户端收到房主刷新后的当前人数 4"), ReusableRooms[0].CurrentPlayers, 4);
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
