/**
 * @file FGameDebugWatchHub.h
 * @brief 现代化通用运行时调试观察中心（按分组拉取状态、固定区域原地刷新与 Shipping 构建零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"

/**
 * @brief 运行时调试观察中心单例（支持任意系统/对象/状态的非侵入式观察与分类控制台开关）
 */
class GAMEDEBUG_API FGameDebugWatchHub {
public:
    using FValueGetter = TFunction<FString()>;

    struct FWatchItem {
        FName Name;
        FValueGetter Getter;
    };

    struct FCategoryGroup {
        FName Category;
        bool bEnabled = true;
        TArray<FWatchItem> Items;
    };

public:
    FGameDebugWatchHub();
    ~FGameDebugWatchHub();

    FGameDebugWatchHub(const FGameDebugWatchHub&) = delete;
    FGameDebugWatchHub& operator=(const FGameDebugWatchHub&) = delete;
    FGameDebugWatchHub(FGameDebugWatchHub&&) = delete;
    FGameDebugWatchHub& operator=(FGameDebugWatchHub&&) = delete;

    /**
     * @brief 获取全局观察中心单例
     * @return 单例引用
     */
    static FGameDebugWatchHub& Get();

    /**
     * @brief 注册一个调试观察项
     * @param Category 业务分类名称（如 "Player", "Tasks", "Memory", "Net"）
     * @param Name 观察变量名称（如 "Speed", "State", "PendingTasks"）
     * @param Getter 获取当前值格式化字符串的闭包
     */
    void Watch(const FName& Category, const FName& Name, FValueGetter Getter);

    /**
     * @brief 注销指定分类下的观察项
     * @param Category 业务分类名称
     * @param Name 观察变量名称
     * @return 存在并成功移除返回 true
     */
    bool Unwatch(const FName& Category, const FName& Name);

    /**
     * @brief 设置指定分类的显示开关
     * @param Category 业务分类名称
     * @param bEnabled 是否显示
     */
    void SetCategoryEnabled(const FName& Category, bool bEnabled);

    /**
     * @brief 查询指定分类是否处于开启显示态
     * @param Category 业务分类名称
     * @return 开启显示返回 true
     */
    [[nodiscard]] bool IsCategoryEnabled(const FName& Category) const;

    /**
     * @brief 设置全局观察显示总开关
     * @param bEnabled 是否全局开启显示
     */
    void SetGlobalEnabled(bool bEnabled) noexcept;

    /**
     * @brief 查询全局观察显示总开关
     * @return 全局开启返回 true
     */
    [[nodiscard]] bool IsGlobalEnabled() const noexcept;

    /**
     * @brief 获取当前注册的所有分类列表
     * @param OutCategories 输出分类名数组
     */
    void GetCategories(TArray<FName>& OutCategories) const;

    /**
     * @brief 清空所有注册的观察项与分类
     */
    void ClearAll();

    /**
     * @brief 每帧收集已启用分类的实时采样值并在屏幕固定区域原地呈现
     * @param DeltaTime 帧流逝时间
     */
    void TickDisplay(float DeltaTime);

private:
    mutable FRWLock RegistryLock;
    TMap<FName, FCategoryGroup> Categories;
    bool bGlobalEnabled = true;
};

#if !UE_BUILD_SHIPPING
    #define GAME_DEBUG_WATCH(Category, Name, Getter) \
        FGameDebugWatchHub::Get().Watch(FName(TEXT(Category)), FName(TEXT(Name)), Getter)

    #define GAME_DEBUG_UNWATCH(Category, Name) \
        FGameDebugWatchHub::Get().Unwatch(FName(TEXT(Category)), FName(TEXT(Name)))
#else
    #define GAME_DEBUG_WATCH(Category, Name, Getter) ((void)0)
    #define GAME_DEBUG_UNWATCH(Category, Name) ((void)0)
#endif
