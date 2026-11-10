/**
 * @file NetTravelLibrary.h
 * @brief 联机地图跳转 URL 防注入词法组装与 ServerTravel / ClientTravel 执行工具库
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/TGameResult.h"
#include "Types/GameNetSessionTypes.h"

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
     * @param MapPath 地图资源路径（空白或含空白控制字符时判为非法并返回空串）
     * @param ListenPort 监听游戏端口号（1 ~ 65535）
     * @param ExtraOptions 可选附加查询串（自动去除前导 '?'）
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
     * @param Room 包含 HostAddress 与 HostGamePort 的房间描述符
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
     * @return URL 合法且触发 ServerTravel 成功返回成功态，否则返回对应错误
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
     * @brief 校验输入文本不含空白或控制字符以防止 Travel URL 选项注入污染
     * @param Token 待校验的路径或主机地址片段
     * @return 非空且不含空白控制字符返回 true
     */
    [[nodiscard]] static bool IsCleanUrlToken(const FString& Token);
};

