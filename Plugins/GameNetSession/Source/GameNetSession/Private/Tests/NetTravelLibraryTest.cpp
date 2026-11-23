/**
 * @file NetTravelLibraryTest.cpp
 * @brief 联机地图跳转 URL 防选项注入组装与执行工具库（UNetTravelLibrary）单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Travel/NetTravelLibrary.h"
#include "Types/NetErrorCodes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetTravelLibraryTest,
    "Game.Net.Session.TravelLibrary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 ListenServer 与 Client Travel URL 组装、?/#///\\ 选项注入防御及统一 NetErrorCodes 错误返回
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
        TEXT("含 '?' 选项注入字符的地图路径被安全拦截"),
        UNetTravelLibrary::BuildListenServerTravelUrl(TEXT("/Game/Maps/Lobby?game=Evil"), 7777).IsEmpty()
    );
    TestTrue(
        TEXT("含 '#' 片段注入字符的地图路径被安全拦截"),
        UNetTravelLibrary::BuildListenServerTravelUrl(TEXT("/Game/Maps/Lobby#anchor"), 7777).IsEmpty()
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
    TestTrue(TEXT("含控制字符的主机地址被拒绝"), UNetTravelLibrary::BuildClientTravelUrl(BadTravelRoom).IsEmpty());

    BadTravelRoom.HostAddress = TEXT("192.168.1.25?game=Evil");
    TestTrue(TEXT("含 '?' 注入字符的主机地址被拒绝"), UNetTravelLibrary::BuildClientTravelUrl(BadTravelRoom).IsEmpty());

    BadTravelRoom.HostAddress = TEXT("192.168.1.25#frag");
    TestTrue(TEXT("含 '#' 注入字符的主机地址被拒绝"), UNetTravelLibrary::BuildClientTravelUrl(BadTravelRoom).IsEmpty());

    BadTravelRoom.HostAddress = TEXT("192.168.1.25/EvilMap");
    TestTrue(TEXT("含 '/' 路径注入字符的主机地址被拒绝"), UNetTravelLibrary::BuildClientTravelUrl(BadTravelRoom).IsEmpty());

    BadTravelRoom.HostAddress = TEXT("192.168.1.25\\EvilMap");
    TestTrue(TEXT("含 '\\\\' 路径注入字符的主机地址被拒绝"), UNetTravelLibrary::BuildClientTravelUrl(BadTravelRoom).IsEmpty());

    const TGameStatusResult<void> MissingWorldRes =
        UNetTravelLibrary::ExecuteListenServerTravel(nullptr, TEXT("/Game/Maps/Lobby"), 7777);
    TestTrue(TEXT("空世界指针调用 ExecuteListenServerTravel 安全返回失败态"), MissingWorldRes.IsFailure());
    TestEqual(TEXT("错误归属 Game::ModuleId::Net"), MissingWorldRes.GetError().ModuleId, Game::ModuleId::Net);
    TestEqual(
        TEXT("空世界错误码为 TravelInvalidArgument"),
        MissingWorldRes.GetError().ErrorCode,
        Game::NetErrorCodes::TravelInvalidArgument
    );

    const TGameStatusResult<void> MissingClientWorldRes =
        UNetTravelLibrary::ExecuteClientRoomTravel(nullptr, TravelRoom);
    TestTrue(TEXT("空世界指针调用 ExecuteClientRoomTravel 安全返回失败态"), MissingClientWorldRes.IsFailure());
    TestEqual(
        TEXT("客户端空世界错误码为 TravelInvalidArgument"),
        MissingClientWorldRes.GetError().ErrorCode,
        Game::NetErrorCodes::TravelInvalidArgument
    );

    const TGameStatusResult<void> EmptyConnectRes =
        UNetTravelLibrary::ExecuteConnectStringTravel(nullptr, TEXT(""));
    TestTrue(TEXT("空连接串调用 ExecuteConnectStringTravel 安全返回失败态"), EmptyConnectRes.IsFailure());
    TestEqual(
        TEXT("错误归属 Game::ModuleId::Net"),
        EmptyConnectRes.GetError().ModuleId,
        Game::ModuleId::Net
    );

    return true;
}

#endif
