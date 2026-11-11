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
     * @brief 注册编译开关桥接键（幂等；当传入非空 InGameInstance 时同时向该 PIE 实例对应的世界隔离配置表注册）
     * @param InGameInstance 当前 GameInstance（可空，模块启动阶段传 nullptr 注册全局根表）
     */
    static void RegisterAll(UGameInstance* InGameInstance = nullptr);

    /**
     * @brief 注册某业务域的缓存策略默认键（Cache.{Domain}.*，幂等；Domain 为 None 时记录 Warning 并忽略）
     * @param Domain 业务域名称（由游戏层提供，框架层不感知具体域名）
     */
    static void RegisterCacheDomain(const FName& Domain);
};
