/**
 * @file FGameDamageChannelRegistry.h
 * @brief 运行时伤害通道绑定注册表（按 ExecCalc 类隔离 · 线程安全）
 *
 * 设计思路：
 * 取代“CDO 构造后永久冻结”的旧契约：通道绑定既可以继续在子类构造函数中声明，
 * 也可以在运行期由任意系统动态注册/注销；查询时按类继承链合并，派生类同名通道覆盖基类，
 * 避免基类与派生类重复绑定同一个通道导致数值双算。
 */

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "UObject/ObjectKey.h"

#include "Damage/UGameDamageExecution.h"

/**
 * @brief 运行时通道注册表（进程级单例）
 */
class GAMEGAMEPLAY_API FGameDamageChannelRegistry {
public:
    FGameDamageChannelRegistry(const FGameDamageChannelRegistry&) = delete;
    FGameDamageChannelRegistry& operator=(const FGameDamageChannelRegistry&) = delete;
    FGameDamageChannelRegistry(FGameDamageChannelRegistry&&) = delete;
    FGameDamageChannelRegistry& operator=(FGameDamageChannelRegistry&&) = delete;

    /**
     * @brief 获取全局注册表
     * @return 注册表实例
     */
    static FGameDamageChannelRegistry& Get();

    /**
     * @brief 注册（或覆盖）一条通道绑定
     * @param InExecutionClass 归属的 ExecCalc 类
     * @param InBinding 通道绑定
     */
    void Register(const UClass* InExecutionClass, const FDamageChannelBinding& InBinding);

    /**
     * @brief 注销指定类的单条通道绑定
     * @param InExecutionClass 归属的 ExecCalc 类
     * @param InChannelTag 通道标签
     * @return 是否注销成功
     */
    bool Unregister(const UClass* InExecutionClass, const FGameplayTag& InChannelTag);

    /**
     * @brief 注销指定类的全部通道绑定
     * @param InExecutionClass 归属的 ExecCalc 类
     * @return 被注销的绑定数量
     */
    int32 UnregisterAll(const UClass* InExecutionClass);

    /**
     * @brief 查询指定类的合并绑定（沿继承链合并，派生类同名通道优先）
     * @param InExecutionClass 目标 ExecCalc 类
     * @param OutBindings 合并结果
     */
    void GetBindings(const UClass* InExecutionClass, TArray<FDamageChannelBinding>& OutBindings) const;

    /**
     * @brief 查询指定类直属的绑定数量（不含继承）
     * @param InExecutionClass 目标 ExecCalc 类
     * @return 绑定数量
     */
    int32 Num(const UClass* InExecutionClass) const;

    /**
     * @brief 清空注册表（测试与热重载使用）
     */
    void Reset();

private:
    FGameDamageChannelRegistry() = default;

    mutable FRWLock RWLock;
    TMap<TObjectKey<const UClass>, TArray<FDamageChannelBinding>> BindingsByClass;
};
