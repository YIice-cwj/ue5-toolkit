/**
 * @file LanRoomDiscoveryTableTest.cpp
 * @brief 局域网房间发现缓存表（FLanRoomDiscoveryTable）多房主广播探测 RTT、输入防注入、容量上限与过期淘汰单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "LAN/LanRoomDiscoveryTable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetLanRoomDiscoveryTableTest,
    "Game.Net.Session.LanRoomDiscoveryTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多房主共享广播探测序号 RTT 测算、来源地址回填、畸形字段防注入、容量防洪泛上限及超时剔除
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetLanRoomDiscoveryTableTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLanRoomDiscoveryTable Table(2.0f);
    TArray<FString> ExpiredIds;
    TArray<FGameNetRoomDescriptor> ReusableSnapshot;

    TestEqual(TEXT("初始房间数为 0"), Table.GetRoomCount(), 0);
    Table.PruneExpiredRooms(10.0, ExpiredIds);
    TestEqual(TEXT("空表过期扫描返回空数组"), ExpiredIds.Num(), 0);

    FGameNetRoomDescriptor InvalidRoom;
    InvalidRoom.RoomId.Empty();
    InvalidRoom.RoomName = TEXT("NoId");
    TestFalse(
        TEXT("缺失 RoomId 的描述符被拒绝"),
        Table.UpsertDiscoveredRoom(InvalidRoom, TEXT("192.168.1.10"), 0, 1.0).IsSet()
    );

    FGameNetRoomDescriptor BadPortRoom;
    BadPortRoom.RoomId = TEXT("BadPort");
    BadPortRoom.RoomName = TEXT("BadPortRoom");
    BadPortRoom.HostGamePort = 0;
    TestFalse(
        TEXT("非法端口 0 的描述符被拒绝"),
        Table.UpsertDiscoveredRoom(BadPortRoom, TEXT("192.168.1.10"), 0, 1.0).IsSet()
    );

    FGameNetRoomDescriptor BadPlayersRoom;
    BadPlayersRoom.RoomId = TEXT("BadPlayers");
    BadPlayersRoom.RoomName = TEXT("BadPlayersRoom");
    BadPlayersRoom.MaxPlayers = 4;
    BadPlayersRoom.CurrentPlayers = 5;
    TestFalse(
        TEXT("当前人数大于最大人数的描述符被拒绝"),
        Table.UpsertDiscoveredRoom(BadPlayersRoom, TEXT("192.168.1.10"), 0, 1.0).IsSet()
    );

    FGameNetRoomDescriptor InjectedHostRoom;
    InjectedHostRoom.RoomId = TEXT("InjectedHost");
    InjectedHostRoom.RoomName = TEXT("InjectedHostRoom");
    InjectedHostRoom.HostAddress = TEXT("192.168.1.20?game=Evil");
    TestFalse(
        TEXT("含 URL 选项注入字符的主机地址被拒绝"),
        Table.UpsertDiscoveredRoom(InjectedHostRoom, TEXT("192.168.1.20"), 0, 1.0).IsSet()
    );

    FGameNetRoomDescriptor MissingHostRoom;
    MissingHostRoom.RoomId = TEXT("MissingHost");
    MissingHostRoom.RoomName = TEXT("MissingHostRoom");
    MissingHostRoom.HostAddress.Empty();
    TestFalse(
        TEXT("HostAddress 与 SenderIp 均为空时被拒绝"),
        Table.UpsertDiscoveredRoom(MissingHostRoom, TEXT(""), 0, 1.0).IsSet()
    );

    const uint32 BroadcastProbeSeq = Table.AllocateAndRecordProbe(10.0);
    TestEqual(TEXT("首个广播探测序列号为 1"), BroadcastProbeSeq, 1u);

    FGameNetRoomDescriptor Host1;
    Host1.RoomId = TEXT("Room_Host1");
    Host1.RoomName = TEXT("Arena_1");
    Host1.MaxPlayers = 4;
    Host1.CurrentPlayers = 1;

    FGameNetRoomDescriptor Host2;
    Host2.RoomId = TEXT("Room_Host2");
    Host2.RoomName = TEXT("Arena_2");
    Host2.MaxPlayers = 8;
    Host2.CurrentPlayers = 2;

    FGameNetRoomDescriptor Host3;
    Host3.RoomId = TEXT("Room_Host3");
    Host3.RoomName = TEXT("Arena_3");
    Host3.MaxPlayers = 4;
    Host3.CurrentPlayers = 3;

    const TOptional<FGameNetRoomDescriptor> Reply1 =
        Table.UpsertDiscoveredRoom(Host1, TEXT("192.168.1.21"), BroadcastProbeSeq, 10.015);
    const TOptional<FGameNetRoomDescriptor> Reply2 =
        Table.UpsertDiscoveredRoom(Host2, TEXT("192.168.1.22"), BroadcastProbeSeq, 10.042);
    const TOptional<FGameNetRoomDescriptor> Reply3 =
        Table.UpsertDiscoveredRoom(Host3, TEXT("192.168.1.23"), BroadcastProbeSeq, 10.088);

    TestTrue(TEXT("多房主广播应答 1 插入成功"), Reply1.IsSet());
    TestTrue(TEXT("多房主广播应答 2 插入成功"), Reply2.IsSet());
    TestTrue(TEXT("多房主广播应答 3 插入成功"), Reply3.IsSet());
    if (Reply1.IsSet() && Reply2.IsSet() && Reply3.IsSet()) {
        TestEqual(TEXT("空 HostAddress 自动回填发送方 IP"), Reply1->HostAddress, FString(TEXT("192.168.1.21")));
        TestEqual(TEXT("房主 1 共享广播探测序号算出 15ms Ping"), Reply1->PingMilliseconds, 15);
        TestEqual(TEXT("房主 2 共享广播探测序号算出 42ms Ping"), Reply2->PingMilliseconds, 42);
        TestEqual(TEXT("房主 3 共享广播探测序号算出 88ms Ping"), Reply3->PingMilliseconds, 88);
    }
    TestEqual(TEXT("表内房间数量为 3"), Table.GetRoomCount(), 3);

    Host1.CurrentPlayers = 3;
    Host1.PingMilliseconds = 0;
    const TOptional<FGameNetRoomDescriptor> Updated =
        Table.UpsertDiscoveredRoom(Host1, TEXT("192.168.1.21"), 0, 11.0);
    TestTrue(TEXT("同 RoomId 刷新成功"), Updated.IsSet());
    if (Updated.IsSet()) {
        TestEqual(TEXT("无回显主动宣告保留此前测算的 15ms Ping"), Updated->PingMilliseconds, 15);
        TestEqual(TEXT("当前人数已刷新为 3"), Updated->CurrentPlayers, 3);
    }
    TestEqual(TEXT("同 RoomId 原地去重保持 3 条记录"), Table.GetRoomCount(), 3);

    Table.GetRoomsSnapshot(ReusableSnapshot);
    TestEqual(TEXT("就地快照中数量为 3"), ReusableSnapshot.Num(), 3);

    const uint32 OrphanProbeSeq = Table.AllocateAndRecordProbe(10.0);
    TestEqual(TEXT("分配第二个探测序列号"), OrphanProbeSeq, 2u);

    Table.PruneExpiredRooms(11.5, ExpiredIds);
    TestEqual(TEXT("未超 2.0s 过期阈值时不剔除"), ExpiredIds.Num(), 0);

    Table.PruneExpiredRooms(13.1, ExpiredIds);
    TestEqual(TEXT("超过 2.0s 阈值后精确剔除全部 3 个过期房间"), ExpiredIds.Num(), 3);
    TestEqual(TEXT("剔除后表内房间归零"), Table.GetRoomCount(), 0);

    FGameNetRoomDescriptor LateRoom;
    LateRoom.RoomId = TEXT("Room_Late");
    LateRoom.RoomName = TEXT("LateArena");
    LateRoom.MaxPlayers = 8;
    const TOptional<FGameNetRoomDescriptor> LateInserted =
        Table.UpsertDiscoveredRoom(LateRoom, TEXT("192.168.1.30"), OrphanProbeSeq, 14.0);
    TestTrue(TEXT("延迟插入房间成功"), LateInserted.IsSet());
    if (LateInserted.IsSet()) {
        TestEqual(TEXT("已过期淘汰的探测序号不再计算陈旧时延"), LateInserted->PingMilliseconds, 0);
    }

    Table.Reset();
    TestEqual(TEXT("Reset 后缓存完全清空"), Table.GetRoomCount(), 0);

    for (int32 Index = 0; Index < FLanRoomDiscoveryTable::MAX_DISCOVERED_ROOMS; ++Index) {
        FGameNetRoomDescriptor CapRoom;
        CapRoom.RoomId = FString::Printf(TEXT("CapRoom_%d"), Index);
        CapRoom.RoomName = CapRoom.RoomId;
        CapRoom.MaxPlayers = 4;
        TestTrue(
            TEXT("上限内新房间允许插入"),
            Table.UpsertDiscoveredRoom(CapRoom, TEXT("192.168.1.50"), 0, 20.0).IsSet()
        );
    }
    TestEqual(TEXT("房间数达到防洪泛上限"), Table.GetRoomCount(), FLanRoomDiscoveryTable::MAX_DISCOVERED_ROOMS);

    FGameNetRoomDescriptor OverflowRoom;
    OverflowRoom.RoomId = TEXT("OverflowRoom_Extra");
    OverflowRoom.RoomName = TEXT("Overflow");
    OverflowRoom.MaxPlayers = 4;
    TestFalse(
        TEXT("超过 MAX_DISCOVERED_ROOMS 上限的新房间被拒绝"),
        Table.UpsertDiscoveredRoom(OverflowRoom, TEXT("192.168.1.99"), 0, 20.0).IsSet()
    );

    FGameNetRoomDescriptor RefreshExisting;
    RefreshExisting.RoomId = TEXT("CapRoom_0");
    RefreshExisting.RoomName = TEXT("CapRoom_0_Updated");
    RefreshExisting.MaxPlayers = 4;
    RefreshExisting.CurrentPlayers = 2;
    TestTrue(
        TEXT("满容状态下已存在房间仍可原地刷新"),
        Table.UpsertDiscoveredRoom(RefreshExisting, TEXT("192.168.1.50"), 0, 20.5).IsSet()
    );

    RefreshExisting.Reset();
    TestTrue(TEXT("Reset 后 RoomId 为空"), RefreshExisting.RoomId.IsEmpty());
    TestEqual(TEXT("Reset 后默认端口为 7777"), RefreshExisting.HostGamePort, 7777);

    return true;
}

#endif
