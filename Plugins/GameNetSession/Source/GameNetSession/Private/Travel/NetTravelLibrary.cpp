// Copyright Epic Games, Inc. All Rights Reserved.

#include "Travel/NetTravelLibrary.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

/**
 * @brief 构造规范化防注入的 ListenServer 地图跳转 URL
 * @param MapPath 地图资源路径
 * @param ListenPort 监听游戏端口号（1 ~ 65535）
 * @param ExtraOptions 可选附加查询串
 * @return 组装后的 Travel URL；参数非法时返回空字符串
 */
FString UNetTravelLibrary::BuildListenServerTravelUrl(
    const FString& MapPath,
    int32 ListenPort,
    const FString& ExtraOptions
) {
    const FString TrimmedMap = MapPath.TrimStartAndEnd();
    if (!IsCleanUrlToken(TrimmedMap) || ListenPort <= 0 || ListenPort > 65535) {
        return FString();
    }

    FString Url = FString::Printf(TEXT("%s?listen?port=%d"), *TrimmedMap, ListenPort);
    FString TrimmedExtra = ExtraOptions.TrimStartAndEnd();
    while (TrimmedExtra.StartsWith(TEXT("?"))) {
        TrimmedExtra.RightChopInline(1, EAllowShrinking::No);
    }

    if (!TrimmedExtra.IsEmpty()) {
        if (!IsCleanUrlToken(TrimmedExtra)) {
            return FString();
        }
        Url += TEXT("?");
        Url += TrimmedExtra;
    }

    return Url;
}

/**
 * @brief 根据房间描述符构造规范化防注入的客户端连接 URL
 * @param Room 包含 HostAddress 与 HostGamePort 的房间描述符
 * @param ExtraOptions 可选附加查询串
 * @return 组装后的客户端 Travel URL；地址或端口非法时返回空字符串
 */
FString UNetTravelLibrary::BuildClientTravelUrl(
    const FGameNetRoomDescriptor& Room,
    const FString& ExtraOptions
) {
    const FString TrimmedHost = Room.HostAddress.TrimStartAndEnd();
    if (!IsCleanUrlToken(TrimmedHost) || Room.HostGamePort <= 0 || Room.HostGamePort > 65535) {
        return FString();
    }

    FString Url = FString::Printf(TEXT("%s:%d"), *TrimmedHost, Room.HostGamePort);
    FString TrimmedExtra = ExtraOptions.TrimStartAndEnd();
    while (TrimmedExtra.StartsWith(TEXT("?"))) {
        TrimmedExtra.RightChopInline(1, EAllowShrinking::No);
    }

    if (!TrimmedExtra.IsEmpty()) {
        if (!IsCleanUrlToken(TrimmedExtra)) {
            return FString();
        }
        Url += TEXT("?");
        Url += TrimmedExtra;
    }

    return Url;
}

/**
 * @brief 在指定世界上执行 ListenServer 地图跳转
 * @param World 目标世界指针
 * @param MapPath 目标地图路径
 * @param ListenPort 监听游戏端口
 * @param ExtraOptions 可选附加查询选项
 * @return URL 合法且触发 ServerTravel 成功返回成功态，否则返回对应错误
 */
TGameStatusResult<void> UNetTravelLibrary::ExecuteListenServerTravel(
    UWorld* World,
    const FString& MapPath,
    int32 ListenPort,
    const FString& ExtraOptions
) {
    if (World == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, TEXT("World pointer is null"))
        );
    }

    const FString TravelUrl = BuildListenServerTravelUrl(MapPath, ListenPort, ExtraOptions);
    if (TravelUrl.IsEmpty()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, TEXT("Failed to build valid listen server travel URL"))
        );
    }

    const bool bSucceeded = World->ServerTravel(TravelUrl, false, false);
    if (!bSucceeded) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2002, TEXT("ServerTravel execution returned false"))
        );
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 根据房间描述符驱动指定世界的首个本地 PlayerController 发起绝对地址 ClientTravel
 * @param World 目标世界指针
 * @param Room 目标房间描述符
 * @param ExtraOptions 可选附加连接参数
 * @return URL 合法且本地 PlayerController 发起跳转返回成功态，否则返回对应错误
 */
TGameStatusResult<void> UNetTravelLibrary::ExecuteClientRoomTravel(
    UWorld* World,
    const FGameNetRoomDescriptor& Room,
    const FString& ExtraOptions
) {
    const FString TravelUrl = BuildClientTravelUrl(Room, ExtraOptions);
    return ExecuteConnectStringTravel(World, TravelUrl);
}

/**
 * @brief 使用平台会话解析出的连接地址串驱动首个本地 PlayerController 发起绝对地址 ClientTravel
 * @param World 目标世界指针
 * @param ConnectString 平台解析出的连接地址字符串
 * @return 地址合法且本地 PlayerController 发起跳转返回成功态，否则返回对应错误
 */
TGameStatusResult<void> UNetTravelLibrary::ExecuteConnectStringTravel(UWorld* World, const FString& ConnectString) {
    if (World == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, TEXT("World pointer is null"))
        );
    }

    const FString TrimmedUrl = ConnectString.TrimStartAndEnd();
    if (!IsCleanUrlToken(TrimmedUrl)) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, TEXT("ConnectString contains invalid characters"))
        );
    }

    UGameInstance* GameInst = World->GetGameInstance();
    if (GameInst == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2001, TEXT("GameInstance is null"))
        );
    }

    APlayerController* PrimaryPC = GameInst->GetFirstLocalPlayerController(World);
    if (PrimaryPC == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2002, TEXT("FirstLocalPlayerController is null"))
        );
    }

    PrimaryPC->ClientTravel(TrimmedUrl, TRAVEL_Absolute);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 校验输入文本不含空白或控制字符以防止 Travel URL 选项注入污染
 * @param Token 待校验的路径或主机地址片段
 * @return 非空且不含空白控制字符返回 true
 */
bool UNetTravelLibrary::IsCleanUrlToken(const FString& Token) {
    if (Token.IsEmpty()) {
        return false;
    }

    for (const TCHAR Ch : Token) {
        if (FChar::IsWhitespace(Ch) || FChar::IsControl(Ch)) {
            return false;
        }
    }
    return true;
}

