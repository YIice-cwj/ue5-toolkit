// Copyright Epic Games, Inc. All Rights Reserved.

#include "Travel/NetTravelLibrary.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Logging/GameLogCategories.h"
#include "Types/NetErrorCodes.h"

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
    if (!IsCleanMapPathToken(TrimmedMap) || ListenPort <= 0 || ListenPort > 65535) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[Travel] 拦截非法 ListenServer 参数: Map=%s, Port=%d"), *TrimmedMap, ListenPort);
        return FString();
    }

    FString Url = FString::Printf(TEXT("%s?listen?port=%d"), *TrimmedMap, ListenPort);
    if (!AppendSanitizedExtraOptions(Url, ExtraOptions)) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[Travel] 拦截含非法字符的 ListenServer ExtraOptions。"));
        return FString();
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
    if (!IsCleanHostToken(TrimmedHost) || Room.HostGamePort <= 0 || Room.HostGamePort > 65535) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[Travel] 拦截非法客户端目标地址: Host=%s, Port=%d"),
            *TrimmedHost,
            Room.HostGamePort
        );
        return FString();
    }

    FString Url = FString::Printf(TEXT("%s:%d"), *TrimmedHost, Room.HostGamePort);
    if (!AppendSanitizedExtraOptions(Url, ExtraOptions)) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[Travel] 拦截含非法字符的客户端 ExtraOptions。"));
        return FString();
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
            Game::ModuleId::Net,
            Game::NetErrorCodes::TravelInvalidArgument,
            TEXT("World pointer is null")
        );
    }

    const FString TravelUrl = BuildListenServerTravelUrl(MapPath, ListenPort, ExtraOptions);
    if (TravelUrl.IsEmpty()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::TravelInvalidArgument,
            TEXT("Failed to build valid listen server travel URL")
        );
    }

    if (!World->ServerTravel(TravelUrl, false, false)) {
        GAME_LOG(Warning, TEXT("[Travel] ServerTravel 执行失败: Url=%s"), *TravelUrl);
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::TravelExecutionFailed,
            TEXT("ServerTravel execution returned false")
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
            Game::ModuleId::Net,
            Game::NetErrorCodes::TravelInvalidArgument,
            TEXT("World pointer is null")
        );
    }

    const FString TrimmedUrl = ConnectString.TrimStartAndEnd();
    if (!IsCleanOptionsToken(TrimmedUrl)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::TravelInvalidArgument,
            TEXT("ConnectString contains invalid characters or is empty")
        );
    }

    UGameInstance* GameInst = World->GetGameInstance();
    if (GameInst == nullptr) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::TravelInvalidArgument,
            TEXT("GameInstance is null")
        );
    }

    APlayerController* PrimaryPC = GameInst->GetFirstLocalPlayerController(World);
    if (PrimaryPC == nullptr) {
        GAME_LOG(Warning, TEXT("[Travel] ClientTravel 失败：未找到本地 PlayerController (Url=%s)。"), *TrimmedUrl);
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::TravelExecutionFailed,
            TEXT("FirstLocalPlayerController is null")
        );
    }

    PrimaryPC->ClientTravel(TrimmedUrl, TRAVEL_Absolute);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 校验基础选项/连接串不含空白或控制字符
 * @param Token 待校验文本
 * @return 非空且不含空白控制字符返回 true
 */
bool UNetTravelLibrary::IsCleanOptionsToken(const FString& Token) noexcept {
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

/**
 * @brief 校验地图资源路径不含空白、控制字符以及 '?'/'#' 选项注入字符
 * @param MapPath 待校验地图路径
 * @return 合法返回 true
 */
bool UNetTravelLibrary::IsCleanMapPathToken(const FString& MapPath) noexcept {
    if (MapPath.IsEmpty()) {
        return false;
    }

    for (const TCHAR Ch : MapPath) {
        if (FChar::IsWhitespace(Ch) || FChar::IsControl(Ch) || Ch == TEXT('?') || Ch == TEXT('#')) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 校验主机地址不含空白、控制字符以及 '?'/'#'/'/'/'\\' 注入字符
 * @param HostAddress 待校验主机地址
 * @return 合法返回 true
 */
bool UNetTravelLibrary::IsCleanHostToken(const FString& HostAddress) noexcept {
    if (HostAddress.IsEmpty()) {
        return false;
    }

    for (const TCHAR Ch : HostAddress) {
        if (FChar::IsWhitespace(Ch) || FChar::IsControl(Ch) || Ch == TEXT('?') || Ch == TEXT('#') || Ch == TEXT('/') || Ch == TEXT('\\')) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 清洗并追加可选 Travel 查询串至目标 URL
 * @param InOutUrl 待追加的目标基础 URL
 * @param ExtraOptions 可选附加查询串
 * @return 附加项为空或校验通过并完成拼接返回 true；含非法注入字符返回 false
 */
bool UNetTravelLibrary::AppendSanitizedExtraOptions(FString& InOutUrl, const FString& ExtraOptions) {
    FString TrimmedExtra = ExtraOptions.TrimStartAndEnd();
    int32 LeadingQuestionIndex = 0;
    while (LeadingQuestionIndex < TrimmedExtra.Len() && TrimmedExtra[LeadingQuestionIndex] == TEXT('?')) {
        ++LeadingQuestionIndex;
    }
    if (LeadingQuestionIndex > 0) {
        TrimmedExtra.RightChopInline(LeadingQuestionIndex, EAllowShrinking::No);
    }

    if (TrimmedExtra.IsEmpty()) {
        return true;
    }
    if (!IsCleanOptionsToken(TrimmedExtra)) {
        return false;
    }

    InOutUrl.Reserve(InOutUrl.Len() + 1 + TrimmedExtra.Len());
    InOutUrl += TEXT("?");
    InOutUrl += TrimmedExtra;
    return true;
}
