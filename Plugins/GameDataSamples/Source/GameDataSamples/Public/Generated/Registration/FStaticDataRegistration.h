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
 * 静态数据控制台命令通过 FAutoConsoleCommandWithWorldArgsAndOutputDevice 在模块装载期
 * 自动完成 RAII 注册与卸载；RegisterAll / UnregisterAll 负责编辑器热更监听会话生命周期。
 */
class GAMEDATASAMPLES_API FStaticDataRegistration {
public:
    FStaticDataRegistration() = delete;

    /**
     * @brief 统一注册（在编辑器构建下启动热更监听会话）
     * @param InGameInstance 当前 GameInstance
     */
    static void RegisterAll(UGameInstance* InGameInstance);

    /**
     * @brief 统一注销（在编辑器构建下递减热更监听会话引用计数）
     * @param InGameInstance 当前 GameInstance
     */
    static void UnregisterAll(UGameInstance* InGameInstance);
};
