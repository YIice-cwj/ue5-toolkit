/**
 * @file FGameCoreRegistration.h
 * @brief GameCore 统一注册类：编译开关桥接键与业务域缓存策略默认键注册
 *
 * 设计思路：各模块各自实现 F{模块}Registration（RegisterAll / UnregisterAll
 * 统一签名），支持聚合注册。业务域缓存策略默认键（Cache.{域}.*）的域清单由游戏层
 * 提供并经 RegisterCacheDomain 逐个注册——框架层不感知任何具体业务域名。
 * UGameInstance 仅前向声明（指针参数），头文件不引入 Engine 依赖。
 */

#pragma once

#include "CoreMinimal.h"

class UGameInstance;

/**
 * @brief GameCore 统一注册类（静态工具类）
 */
class GAMECORE_API FGameCoreRegistration {
public:
    FGameCoreRegistration() = delete;

    /**
     * @brief 注册编译开关桥接键（幂等）
     * @param InGameInstance 当前 GameInstance（可空，模块启动阶段可传 nullptr）
     */
    static void RegisterAll(UGameInstance* InGameInstance);

    /**
     * @brief 注销统一入口（对称接口）
     * @param InGameInstance 当前 GameInstance
     */
    static void UnregisterAll(UGameInstance* InGameInstance);

    /**
     * @brief 注册某业务域的缓存策略默认键（Cache.{Domain}.*，幂等）
     * @param Domain 业务域名称（由游戏层提供，框架层不感知具体域名）
     */
    static void RegisterCacheDomain(const FName& Domain);
};
