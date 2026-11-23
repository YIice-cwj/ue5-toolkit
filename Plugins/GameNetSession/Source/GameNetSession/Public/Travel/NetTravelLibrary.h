/**
 * @file NetTravelLibrary.h
 * @brief 联机地图跳转 URL 防选项注入词法组装与 ServerTravel / ClientTravel 执行工具库
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/GameNetSessionTypes.h"
#include "Types/TGameResult.h"

#include "NetTravelLibrary.generated.h"

class UWorld;

/**
 * @brief 提供安全的联机 Travel URL 组装及服务端/客户端地图跳转执行的无状态蓝图函数库
 */
UCLASS()
class GAMENETSESSION_API UNetTravelLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 构造规范化防注入的 ListenServer 地图跳转 URL
     * @param MapPath 地图资源路径（禁止空白、控制字符及 '?'/'#' 选项注入字符）
     * @param ListenPort 监听游戏端口号（1 ~ 65535）
     * @param ExtraOptions 可选附加查询串（自动去除前导 '?'，禁止空白与控制字符）
     * @return 组装后的 Travel URL；参数非法时返回空字符串
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Travel")
    static FString BuildListenServerTravelUrl(
        const FString& MapPath,
        int32 ListenPort = 7777,
        const FString& ExtraOptions = TEXT("")
    );

    /**
     * @brief 根据房间描述符构造规范化防注入的客户端连接 URL
     * @param Room 包含 HostAddress 与 HostGamePort 的房间描述符（HostAddress 禁止 '?'/'#'/'/'/'\\' 注入字符）
     * @param ExtraOptions 可选附加查询串（自动去除前导 '?'）
     * @return 组装后的客户端 Travel URL；地址或端口非法时返回空字符串
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Travel")
    static FString BuildClientTravelUrl(
        const FGameNetRoomDescriptor& Room,
        const FString& ExtraOptions = TEXT("")
    );

    /**
     * @brief 在指定世界上执行 ListenServer 地图跳转
     * @param World 目标世界指针
     * @param MapPath 目标地图路径
     * @param ListenPort 监听游戏端口
     * @param ExtraOptions 可选附加查询选项
     * @return URL 合法且触发 ServerTravel 成功返回成功态，否则返回携带 Game::ModuleId::Net 的错误
     */
    static TGameStatusResult<void> ExecuteListenServerTravel(
        UWorld* World,
        const FString& MapPath,
        int32 ListenPort = 7777,
        const FString& ExtraOptions = TEXT("")
    );

    /**
     * @brief 根据房间描述符驱动指定世界的首个本地 PlayerController 发起绝对地址 ClientTravel
     * @param World 目标世界指针
     * @param Room 目标房间描述符
     * @param ExtraOptions 可选附加连接参数
     * @return URL 合法且本地 PlayerController 发起跳转返回成功态，否则返回对应错误
     */
    static TGameStatusResult<void> ExecuteClientRoomTravel(
        UWorld* World,
        const FGameNetRoomDescriptor& Room,
        const FString& ExtraOptions = TEXT("")
    );

    /**
     * @brief 使用平台会话解析出的连接地址串驱动首个本地 PlayerController 发起绝对地址 ClientTravel
     * @param World 目标世界指针
     * @param ConnectString 平台解析出的连接地址字符串
     * @return 地址合法且本地 PlayerController 发起跳转返回成功态，否则返回对应错误
     */
    static TGameStatusResult<void> ExecuteConnectStringTravel(UWorld* World, const FString& ConnectString);

private:
    /**
     * @brief 校验基础选项/连接串不含空白或控制字符
     * @param Token 待校验文本
     * @return 非空且不含空白控制字符返回 true
     */
    [[nodiscard]] static bool IsCleanOptionsToken(const FString& Token) noexcept;

    /**
     * @brief 校验地图资源路径不含空白、控制字符以及 '?'/'#' 选项注入字符
     * @param MapPath 待校验地图路径
     * @return 合法返回 true
     */
    [[nodiscard]] static bool IsCleanMapPathToken(const FString& MapPath) noexcept;

    /**
     * @brief 校验主机地址不含空白、控制字符以及 '?'/'#'/'/'/'\\' 注入字符
     * @param HostAddress 待校验主机地址
     * @return 合法返回 true
     */
    [[nodiscard]] static bool IsCleanHostToken(const FString& HostAddress) noexcept;

    /**
     * @brief 清洗并追加可选 Travel 查询串至目标 URL
     * @param InOutUrl 待追加的目标基础 URL
     * @param ExtraOptions 可选附加查询串
     * @return 附加项为空或校验通过并完成拼接返回 true；含非法注入字符返回 false
     */
    [[nodiscard]] static bool AppendSanitizedExtraOptions(FString& InOutUrl, const FString& ExtraOptions);
};
