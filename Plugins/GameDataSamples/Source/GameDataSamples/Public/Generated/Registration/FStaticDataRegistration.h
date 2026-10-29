/**
 * @file FStaticDataRegistration.h
 * @brief 静态数据统一注册入口
 */
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

/**
 * @brief 静态数据层统一注册类（静态工具类）
 *
 * 设计思路：各模块各自实现 F{模块}Registration（RegisterAll / UnregisterAll
 * 统一签名），由生成器按约定目录自动扫描，聚合成统一注册入口。
 * .bin 导出命令在模块装载期静态注册；其余控制台命令为进程级资源，
 * 仅首次 GameInstance 注册（幂等守卫），不随会话注销。
 */
class GAMEDATASAMPLES_API FStaticDataRegistration {
public:
    FStaticDataRegistration() = delete;

    /**
     * @brief 统一注册（StaticData.* 配置键 + 控制台命令 + 热更监听）
     * @param InGameInstance 当前 GameInstance（用于解析静态数据子系统）
     */
    static void RegisterAll(UGameInstance* InGameInstance);

    /**
     * @brief 统一注销（停止热更监听；控制台命令为进程级资源不注销）
     * @param InGameInstance 当前 GameInstance（本类未使用）
     */
    static void UnregisterAll(UGameInstance* InGameInstance);
};
