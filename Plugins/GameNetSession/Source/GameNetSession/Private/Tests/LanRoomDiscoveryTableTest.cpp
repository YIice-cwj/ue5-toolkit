/**
 * @file LanRoomDiscoveryTableTest.cpp
 * @brief 局域网房间发现缓存表（FLanRoomDiscoveryTable）探测 RTT 测算、同房去重与过期淘汰单元测试
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
 * @brief 验证探测序列号分配、RTT 四舍五入换算、来源地址回填、同 RoomId 去重及超时剔除
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetLanRoomDiscoveryTableTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLanRoomDiscoveryTable Table(2.0f);
    TestEqual(TEXT("初始房间数为 0"), Table.GetRoomCount(), 0);
    TestEqual(TEXT("空表过期扫描返回空数组"), Table.PruneExpiredRooms(10.0).Num(), 0);

    FGameNetRoomDescriptor InvalidRoom;
    InvalidRoom.RoomId.Empty();
    InvalidRoom.RoomName = TEXT("NoId");
    TestFalse(
        TEXT("缺失 RoomId 的描述符被拒绝"),
        Table.UpsertDiscoveredRoom(InvalidRoom, TEXT("192.168.1.10"), 0, 1.0).IsSet()
    );

    const uint32 ProbeSeq = Table.AllocateAndRecordProbe(10.0);
    TestEqual(TEXT("首个探测序列号为 1"), ProbeSeq, 1u);

    FGameNetRoomDescriptor ValidRoom;
    ValidRoom.RoomId = TEXT("Room_Alpha");
    ValidRoom.RoomName = TEXT("AlphaArena");
    ValidRoom.MaxPlayers = 4;
    ValidRoom.CurrentPlayers = 1;

    const TOptional<FGameNetRoomDescriptor> Inserted =
        Table.UpsertDiscoveredRoom(ValidRoom, TEXT("192.168.1.20"), ProbeSeq, 10.042);
    TestTrue(TEXT("合法房间成功插入缓存表"), Inserted.IsSet());
    if (Inserted.IsSet()) {
        TestEqual(TEXT("空 HostAddress 自动回填发送方 IP"), Inserted->HostAddress, FString(TEXT("192.168.1.20")));
        TestEqual(TEXT("根据探测回显算出 42ms Ping"), Inserted->PingMilliseconds, 42);
    }
    TestEqual(TEXT("表内房间数量为 1"), Table.GetRoomCount(), 1);

    ValidRoom.CurrentPlayers = 3;
    ValidRoom.PingMilliseconds = 0;
    const TOptional<FGameNetRoomDescriptor> Updated =
        Table.UpsertDiscoveredRoom(ValidRoom, TEXT("192.168.1.20"), 0, 11.0);
    TestTrue(TEXT("同 RoomId 刷新成功"), Updated.IsSet());
    TestEqual(TEXT("同 RoomId 原地去重保持 1 条记录"), Table.GetRoomCount(), 1);
    TestEqual(TEXT("快照中人数已刷新为 3"), Table.GetRoomsSnapshot()[0].CurrentPlayers, 3);
    TestEqual(TEXT("无回显的主动宣告保留此前已测算的 42ms Ping"), Table.GetRoomsSnapshot()[0].PingMilliseconds, 42);

    TestEqual(TEXT("未超 2.0s 过期阈值时不剔除"), Table.PruneExpiredRooms(12.5).Num(), 0);
    const TArray<FString> ExpiredIds = Table.PruneExpiredRooms(13.1);
    TestEqual(TEXT("超过 2.0s 阈值后精确剔除 1 个房间"), ExpiredIds.Num(), 1);
    TestEqual(TEXT("剔除的 RoomId 为 Room_Alpha"), ExpiredIds[0], FString(TEXT("Room_Alpha")));
    TestEqual(TEXT("剔除后表内房间归零"), Table.GetRoomCount(), 0);

    Table.UpsertDiscoveredRoom(ValidRoom, TEXT("192.168.1.20"), 0, 20.0);
    Table.Reset();
    TestEqual(TEXT("Reset 后缓存完全清空"), Table.GetRoomCount(), 0);

    return true;
}

#endif

