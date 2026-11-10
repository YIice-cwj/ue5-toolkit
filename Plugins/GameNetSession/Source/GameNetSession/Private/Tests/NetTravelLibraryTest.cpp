/**
 * @file NetTravelLibraryTest.cpp
 * @brief 联机地图跳转 URL 防注入组装与执行工具库（UNetTravelLibrary）单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Travel/NetTravelLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetTravelLibraryTest,
    "Game.Net.Session.TravelLibrary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 ListenServer 与 Client Travel URL 组装、空白/控制字符防注入拦截及空世界安全保护
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetTravelLibraryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestEqual(
        TEXT("构造标准 ListenServer URL"),
        UNetTravelLibrary::BuildListenServerTravelUrl(TEXT("/Game/Maps/Lobby"), 7777, TEXT("?MaxPlayers=4")),
        TEXT("/Game/Maps/Lobby?listen?port=7777?MaxPlayers=4")
    );
    TestTrue(
        TEXT("空白地图路径拒绝生成 ListenServer URL"),
        UNetTravelLibrary::BuildListenServerTravelUrl(TEXT("   "), 7777).IsEmpty()
    );
    TestTrue(
        TEXT("含空白注入字符的地图路径被安全拦截"),
        UNetTravelLibrary::BuildListenServerTravelUrl(TEXT("/Game/Maps/Lobby ?hack=1"), 7777).IsEmpty()
    );
    TestTrue(
        TEXT("含空白字符的附加选项被安全拦截"),
        UNetTravelLibrary::BuildListenServerTravelUrl(TEXT("/Game/Maps/Lobby"), 7777, TEXT("opt=1 bad=2")).IsEmpty()
    );
    TestTrue(
        TEXT("非法监听端口拒绝生成 ListenServer URL"),
        UNetTravelLibrary::BuildListenServerTravelUrl(TEXT("/Game/Maps/Lobby"), 0).IsEmpty()
    );

    FGameNetRoomDescriptor TravelRoom;
    TravelRoom.HostAddress = TEXT("192.168.1.25");
    TravelRoom.HostGamePort = 8888;
    TestEqual(
        TEXT("构造标准客户端 Travel URL"),
        UNetTravelLibrary::BuildClientTravelUrl(TravelRoom, TEXT("token=abc123")),
        TEXT("192.168.1.25:8888?token=abc123")
    );

    FGameNetRoomDescriptor BadTravelRoom = TravelRoom;
    BadTravelRoom.HostAddress = TEXT("192.168.1.25\nmalicious");
    TestTrue(
        TEXT("含控制字符的主机地址被拒绝"),
        UNetTravelLibrary::BuildClientTravelUrl(BadTravelRoom).IsEmpty()
    );

    TestTrue(
        TEXT("空世界指针调用 ExecuteListenServerTravel 安全返回失败态"),
        UNetTravelLibrary::ExecuteListenServerTravel(nullptr, TEXT("/Game/Maps/Lobby"), 7777).IsFailure()
    );
    TestTrue(
        TEXT("空世界指针调用 ExecuteClientRoomTravel 安全返回失败态"),
        UNetTravelLibrary::ExecuteClientRoomTravel(nullptr, TravelRoom).IsFailure()
    );
    TestTrue(
        TEXT("空连接串调用 ExecuteConnectStringTravel 安全返回失败态"),
        UNetTravelLibrary::ExecuteConnectStringTravel(nullptr, TEXT("")).IsFailure()
    );

    return true;
}

#endif

