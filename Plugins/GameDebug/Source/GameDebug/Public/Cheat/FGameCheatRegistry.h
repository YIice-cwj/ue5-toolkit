/**
 * @file FGameCheatRegistry.h
 * @brief 受编译期 GM 开关保护的轻量模块化调试/GM 指令注册中心（Shipping 构建自动零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"

class IConsoleObject;

/**
 * @brief 模块化调试与 GM 作弊指令注册表（自动同步挂载 IConsoleManager 并在 Shipping 下编译期裁剪）
 */
class GAMEDEBUG_API FGameCheatRegistry {
public:
    using FCheatHandler = TFunction<void(const TArray<FString>&)>;

private:
    struct FCheatEntry {
        FString HelpText;
        TSharedPtr<const FCheatHandler> Handler;
        IConsoleObject* ConsoleCommand = nullptr;
    };

public:
    FGameCheatRegistry() = default;
    ~FGameCheatRegistry() = default;

    FGameCheatRegistry(const FGameCheatRegistry&) = delete;
    FGameCheatRegistry& operator=(const FGameCheatRegistry&) = delete;
    FGameCheatRegistry(FGameCheatRegistry&&) = delete;
    FGameCheatRegistry& operator=(FGameCheatRegistry&&) = delete;

    /**
     * @brief 获取全局 GM 指令注册表单例
     * @return 单例引用
     */
    static FGameCheatRegistry& Get();

    /**
     * @brief 注册一条 GM 调试指令并同步绑定引擎控制台命令（Shipping 下静默返回 false）
     * @param CommandName 指令唯一名称（如 TEXT("GM.AddGold")）
     * @param HelpText 控制台帮助说明文案
     * @param Handler 参数列表执行回调
     * @return 注册成功返回 true；GM 开关关闭、名称无效、回调为空或指令重名时返回 false
     */
    bool RegisterCheat(const FName& CommandName, const FString& HelpText, FCheatHandler Handler);

    /**
     * @brief 注销指定名称的 GM 调试指令并解绑对应引擎控制台对象
     * @param CommandName 指令唯一名称
     * @return 存在并成功注销返回 true
     */
    bool UnregisterCheat(const FName& CommandName);

    /**
     * @brief 解析命令行字符串并执行对应的 GM 指令（支持双引号包裹含空格参数，锁外安全派发）
     * @param CommandLine 完整指令行（如 TEXT("GM.GiveItem \"Dragon Sword\" 1")）
     * @return 命中已注册指令并成功执行返回 true
     */
    bool ExecuteCheat(const FString& CommandLine);

    /**
     * @brief 查询指定名称的 GM 指令是否处于已注册状态
     * @param CommandName 指令唯一名称
     * @return 已注册返回 true
     */
    [[nodiscard]] bool HasCheat(const FName& CommandName) const;

    /**
     * @brief 查询当前已注册的活跃 GM 指令数量
     * @return 指令总数
     */
    [[nodiscard]] int32 GetCheatCount() const;

    /**
     * @brief 注销并清空全部已注册 GM 指令及关联控制台命令（模块卸载或测试清理时调用）
     */
    void ClearAll();

private:
    mutable FRWLock RegistryLock;
    TMap<FName, FCheatEntry> Entries;
};
