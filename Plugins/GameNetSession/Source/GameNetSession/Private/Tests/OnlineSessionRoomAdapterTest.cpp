/**
 * @file OnlineSessionRoomAdapterTest.cpp
 * @brief OnlineSubsystem 平台房间会话适配器（FOnlineSessionRoomAdapter）参数校验与无 OSS 降级安全单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Online/OnlineSessionRoomAdapter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetOnlineSessionRoomAdapterTest,
    "Game.Net.Session.OnlineSessionAdapter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证平台会话适配器在非法入参与无平台 OSS 环境下的零崩溃安全降级行为
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetOnlineSessionRoomAdapterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FOnlineSessionRoomAdapter Adapter;

    FGameNetCreateRoomRequest EmptyNameReq;
    EmptyNameReq.RoomName.Empty();
    TestFalse(TEXT("空房间名 CreateRoom 安全拒绝"), Adapter.CreateRoom(nullptr, EmptyNameReq, 0));

    FGameNetCreateRoomRequest ZeroPlayerReq;
    ZeroPlayerReq.MaxPlayers = 0;
    TestFalse(TEXT("零最大人数 CreateRoom 安全拒绝"), Adapter.CreateRoom(nullptr, ZeroPlayerReq, 0));

    FGameNetCreateRoomRequest ValidReq;
    TestFalse(TEXT("负玩家下标 CreateRoom 安全拒绝"), Adapter.CreateRoom(nullptr, ValidReq, -1));

    TestFalse(TEXT("零最大结果数 FindRooms 安全拒绝"), Adapter.FindRooms(nullptr, true, 0, 0));
    TestFalse(TEXT("负玩家下标 FindRooms 安全拒绝"), Adapter.FindRooms(nullptr, true, 10, -1));
    TestFalse(TEXT("未搜索时 JoinRoomByIndex 安全拒绝"), Adapter.JoinRoomByIndex(nullptr, 0, 0));
    TestFalse(TEXT("无活跃命名会话时 DestroyRoom 安全返回 false"), Adapter.DestroyRoom(nullptr));
    TestEqual(TEXT("初始缓存房间列表为空"), Adapter.GetCachedRooms().Num(), 0);

    Adapter.Shutdown(nullptr);
    TestEqual(TEXT("Shutdown 后缓存房间列表保持为空"), Adapter.GetCachedRooms().Num(), 0);

    return true;
}

#endif

