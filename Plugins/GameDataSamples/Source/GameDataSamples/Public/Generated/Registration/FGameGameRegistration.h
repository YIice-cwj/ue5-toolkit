// Automatically generated game layer registration entry. Do not edit manually.
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

/**
 * @brief 游戏层统一注册类（静态工具类）
 *
 * 设计思路：各模块各自实现 F{模块}Registration（RegisterAll / UnregisterAll
 * 统一签名），由生成器按约定目录自动扫描，聚合成统一注册入口。
 * 注册项（键名与键值）取自配置，业务域清单取自数据表域集合，本类不含任何业务字眼。
 */
class GAMEDATASAMPLES_API FGameGameRegistration {
public:
    FGameGameRegistration() = delete;

    /**
     * @brief 统一注册（注册游戏域配置默认键 + 提交业务域缓存策略域清单）
     * @param InGameInstance 当前 GameInstance（本类未使用，保持统一签名）
     */
    static void RegisterAll(UGameInstance* InGameInstance);

    /**
     * @brief 统一注销（配置无生命周期，空实现保持统一入口对称）
     * @param InGameInstance 当前 GameInstance（本类未使用，保持统一签名）
     */
    static void UnregisterAll(UGameInstance* InGameInstance);
};
