/**
 * @file GameRegistration.h
 * @brief 统一注册入口（由 Tools/Excel/Script 管线自动生成，请勿手动编辑）
 *
 * 自动扫描各模块 Public/ 下 F{模块}Registration 注册类并聚合；
 * RegisterAll 按类名正序执行，UnregisterAll 按反序注销。
 */

#pragma once

#include "CoreMinimal.h"

class UGameInstance;

#include "Registration/FGameCoreRegistration.h"
#include "Generated/Registration/FGameGameRegistration.h"
#include "Generated/Registration/FStaticDataRegistration.h"

/**
 * @brief 统一注册入口（静态工具类）
 *
 * 由 UGameGameInstance::Init / Shutdown 各调用一次。
 */
class FGameRegistration {
public:
    FGameRegistration() = delete;

    /**
     * @brief 统一注册（配置键 / 控制台命令 / 运行时装配）
     * @param InGameInstance 当前 GameInstance
     */
    static void RegisterAll(UGameInstance* InGameInstance) {
        FGameCoreRegistration::RegisterAll(InGameInstance);
        FGameGameRegistration::RegisterAll(InGameInstance);
        FStaticDataRegistration::RegisterAll(InGameInstance);
    }

    /**
     * @brief 统一注销（与注册反序）
     * @param InGameInstance 当前 GameInstance
     */
    static void UnregisterAll(UGameInstance* InGameInstance) {
        FStaticDataRegistration::UnregisterAll(InGameInstance);
        FGameGameRegistration::UnregisterAll(InGameInstance);
        FGameCoreRegistration::UnregisterAll(InGameInstance);
    }
};
