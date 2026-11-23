/**
 * @file LanBeaconBroadcasterTest.cpp
 * @brief 局域网房间信标协调器（FLanBeaconBroadcaster）探测应答闭环、预缓存刷新、畸形包防御与重入安全过期通知单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Codec/NetPacketCodec.h"
#include "LAN/LanBeaconBroadcaster.h"
#include "LAN/LanUdpSocketChannel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetLanBeaconBroadcasterTest,
    "Game.Net.Session.LanBeacon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证局域网信标广播与发现闭环、预编码缓存动态刷新、外部畸形数据报过滤及过期回调重入 Stop() 安全性
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetLanBeaconBroadcasterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLanBeaconBroadcaster HostBeacon(2.0f);
    FLanBeaconBroadcaster ClientBeacon(2.0f);

    TestFalse(TEXT("未激活时 IsActive 为 false"), HostBeacon.IsActive());
    TestEqual(TEXT("初始角色为 Inactive"), HostBeacon.GetRole(), EGameNetLanRole::Inactive);
    TestFalse(TEXT("未激活时发送探测返回 false"), ClientBeacon.SendDiscoveryProbe());
    TestFalse(TEXT("未激活时发送宣告返回 false"), HostBeacon.BroadcastAnnouncement());
    TestFalse(TEXT("未激活时等待可读返回 false"), ClientBeacon.WaitForIncomingData(0.01));

    FGameNetRoomDescriptor InvalidRoom;
    InvalidRoom.RoomName = TEXT("");
    TestFalse(TEXT("空房间名拒绝启动广播"), HostBeacon.StartHosting(InvalidRoom, 0, TEXT("127.0.0.1")));

    InvalidRoom.RoomName = TEXT("ValidName");
    InvalidRoom.MaxPlayers = 0;
    TestFalse(TEXT("非法最大人数拒绝启动广播"), HostBeacon.StartHosting(InvalidRoom, 0, TEXT("127.0.0.1")));

    FGameNetRoomDescriptor HostedDesc;
    HostedDesc.RoomName = TEXT("TaoistArena_01");
    HostedDesc.MapPath = TEXT("/Game/Maps/SectArena");
    HostedDesc.HostGamePort = 17777;
    HostedDesc.CurrentPlayers = 1;
    HostedDesc.MaxPlayers = 4;

    TestTrue(TEXT("房主绑定本地环回空闲端口成功"), HostBeacon.StartHosting(HostedDesc, 0, TEXT("127.0.0.1")));
    TestTrue(TEXT("房主启动后自动分配有效非零端口"), HostBeacon.GetBoundPort() > 0);
    TestFalse(TEXT("房主自动生成非空唯一 RoomId"), HostBeacon.GetHostedRoom().RoomId.IsEmpty());
    TestFalse(TEXT("Hosting 角色调用 SendDiscoveryProbe 被拒绝"), HostBeacon.SendDiscoveryProbe());

    TestTrue(TEXT("客户端绑定本地环回空闲端口成功"), ClientBeacon.StartDiscovery(0, TEXT("127.0.0.1")));
    TestTrue(TEXT("客户端启动后自动分配有效非零端口"), ClientBeacon.GetBoundPort() > 0);
    TestFalse(TEXT("Discovering 角色调用 BroadcastAnnouncement 被拒绝"), ClientBeacon.BroadcastAnnouncement());

    TArray<FGameNetRoomDescriptor> DiscoveredEvents;
    TArray<FString> ExpiredEvents;
    ClientBeacon.OnRoomDiscovered().AddLambda([&DiscoveredEvents](const FGameNetRoomDescriptor& Room) {
        DiscoveredEvents.Add(Room);
    });
    ClientBeacon.OnRoomExpired().AddLambda([&ExpiredEvents, &ClientBeacon](const FString& RoomId) {
        ExpiredEvents.Add(RoomId);
        ClientBeacon.Stop();
    });

    FGameNetLanEndpoint HostEndpoint;
    HostEndpoint.IpAddress = TEXT("127.0.0.1");
    HostEndpoint.Port = HostBeacon.GetBoundPort();
    TestTrue(TEXT("客户端向房主端口发送探测成功"), ClientBeacon.SendDiscoveryProbe(HostEndpoint));

    TestTrue(TEXT("房主内核事件驱动感知探测数据报到达"), HostBeacon.WaitForIncomingData(1.0));
    HostBeacon.Tick(0.01f);

    TestTrue(TEXT("客户端内核事件驱动感知宣告回包到达"), ClientBeacon.WaitForIncomingData(1.0));
    ClientBeacon.Tick(0.035f);

    TArray<FGameNetRoomDescriptor> Snapshot;
    ClientBeacon.GetDiscoveredRooms(Snapshot);
    TestEqual(TEXT("客户端成功发现 1 个局域网房间"), Snapshot.Num(), 1);
    TestEqual(TEXT("房间发现委托触发 1 次"), DiscoveredEvents.Num(), 1);

    if (Snapshot.Num() == 1) {
        TestEqual(TEXT("发现房间 ID 与房主一致"), Snapshot[0].RoomId, HostBeacon.GetHostedRoom().RoomId);
        TestEqual(TEXT("发现房间名称还原无损"), Snapshot[0].RoomName, TEXT("TaoistArena_01"));
        TestEqual(TEXT("未显式指定 HostAddress 时自动回填发送方 IP"), Snapshot[0].HostAddress, TEXT("127.0.0.1"));
        TestEqual(TEXT("根据回显 SequenceId 精确算出虚拟时钟 35ms Ping"), Snapshot[0].PingMilliseconds, 35);
    }

    FGameNetRoomDescriptor UpdatedDesc = HostBeacon.GetHostedRoom();
    UpdatedDesc.RoomId.Empty();
    UpdatedDesc.CurrentPlayers = 3;
    TestTrue(TEXT("合法 UpdateHostedRoom 提交成功"), HostBeacon.UpdateHostedRoom(UpdatedDesc));
    TestEqual(TEXT("UpdateHostedRoom 保留原有 RoomId"), HostBeacon.GetHostedRoom().RoomId, Snapshot[0].RoomId);

    FGameNetRoomDescriptor InvalidUpdateDesc = HostBeacon.GetHostedRoom();
    InvalidUpdateDesc.RoomName.Empty();
    TestFalse(TEXT("空房间名 UpdateHostedRoom 被拒绝"), HostBeacon.UpdateHostedRoom(InvalidUpdateDesc));
    TestEqual(TEXT("非法更新失败后保留原有房间名"), HostBeacon.GetHostedRoom().RoomName, FString(TEXT("TaoistArena_01")));

    FGameNetRoomDescriptor OversizedUpdateDesc = HostBeacon.GetHostedRoom();
    OversizedUpdateDesc.MapPath = FString::ChrN(5000, TEXT('A'));
    TestFalse(TEXT("超 4096B 上限的 UpdateHostedRoom 被拒绝"), HostBeacon.UpdateHostedRoom(OversizedUpdateDesc));
    TestEqual(TEXT("超限更新失败后保留原有合法人数与缓存"), HostBeacon.GetHostedRoom().CurrentPlayers, 3);

    FGameNetLanEndpoint ClientEndpoint;
    ClientEndpoint.IpAddress = TEXT("127.0.0.1");
    ClientEndpoint.Port = ClientBeacon.GetBoundPort();
    TestTrue(TEXT("房主复用预编码缓存主动推送更新宣告成功"), HostBeacon.BroadcastAnnouncement(ClientEndpoint));

    TestTrue(TEXT("客户端感知主动宣告到达"), ClientBeacon.WaitForIncomingData(1.0));
    ClientBeacon.Tick(0.01f);
    ClientBeacon.GetDiscoveredRooms(Snapshot);
    TestEqual(TEXT("相同 RoomId 原地更新去重不产生重复条目"), Snapshot.Num(), 1);
    if (Snapshot.Num() == 1) {
        TestEqual(TEXT("房间当前人数已刷新为 3"), Snapshot[0].CurrentPlayers, 3);
    }

    FLanUdpSocketChannel MaliciousInjector;
    TestTrue(TEXT("打开畸形包注入通道"), MaliciousInjector.Open(TEXT("127.0.0.1"), 0));
    const uint8 ShortGarbage[] = {0x01, 0x02, 0x03};
    MaliciousInjector.SendTo(ShortGarbage, TEXT("127.0.0.1"), ClientBeacon.GetBoundPort());

    FNetPacketHeader BadJsonHeader;
    BadJsonHeader.MessageId = FLanBeaconBroadcaster::LAN_ROOM_ANNOUNCE_MSG_ID;
    const uint8 CorruptJsonBytes[] = {'{', 'b', 'a', 'd'};
    TArray<uint8> BadJsonFrame;
    FNetPacketCodec::EncodeFrameTo(BadJsonHeader, CorruptJsonBytes, BadJsonFrame);
    MaliciousInjector.SendTo(BadJsonFrame, TEXT("127.0.0.1"), ClientBeacon.GetBoundPort());

    TArray<uint8> TruncatedFrame = BadJsonFrame;
    TruncatedFrame.Pop();
    MaliciousInjector.SendTo(TruncatedFrame, TEXT("127.0.0.1"), ClientBeacon.GetBoundPort());

    ClientBeacon.WaitForIncomingData(0.2);
    ClientBeacon.Tick(0.01f);
    ClientBeacon.GetDiscoveredRooms(Snapshot);
    TestEqual(TEXT("所有畸形 UDP 数据报均被安全丢弃且不影响现有房间缓存"), Snapshot.Num(), 1);

    ClientBeacon.Tick(2.5f);
    ClientBeacon.GetDiscoveredRooms(Snapshot);
    TestEqual(TEXT("超过静默过期阈值后房间自动淘汰"), Snapshot.Num(), 0);
    TestEqual(TEXT("触发 1 次房间过期通知"), ExpiredEvents.Num(), 1);
    TestFalse(TEXT("过期回调内重入调用 Stop 后信标安全停用"), ClientBeacon.IsActive());

    HostBeacon.Stop();
    TestEqual(TEXT("Stop 后端口归零"), HostBeacon.GetBoundPort(), 0);
    TestFalse(TEXT("Stop 后状态变为非活跃"), HostBeacon.IsActive());
    return true;
}

#endif
