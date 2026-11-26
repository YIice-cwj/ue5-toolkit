/**
 * @file FGameDebugWatchHub.h
 * @brief 现代化通用运行时调试观察中心（按分组拉取状态、独立域盐原地刷新、零闭包拷贝快照与 Shipping 零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "Config/GameCompileSwitches.h"
#include "Misc/ScopeRWLock.h"

/**
 * @brief 运行时调试观察中心单例（支持任意系统/对象/状态的非侵入式观察与分类控制台开关）
 */
class GAMEDEBUG_API FGameDebugWatchHub final {
public:
    using FValueGetter = TFunction<FString()>;

private:
    struct FWatchItem {
        FName Name;
        TSharedPtr<const FValueGetter> Getter;
    };

    struct FCategoryGroup {
        bool bEnabled = true;
        TArray<FWatchItem> Items;
    };

public:
    FGameDebugWatchHub() = default;
    ~FGameDebugWatchHub() = default;

    FGameDebugWatchHub(const FGameDebugWatchHub&) = delete;
    FGameDebugWatchHub& operator=(const FGameDebugWatchHub&) = delete;
    FGameDebugWatchHub(FGameDebugWatchHub&&) = delete;
    FGameDebugWatchHub& operator=(FGameDebugWatchHub&&) = delete;

    /**
     * @brief 初始化观察中心并向 GM 注册表挂载 GM.Watch 控制台指令（幂等，模块启动时调用）
     */
    void Initialize();

    /**
     * @brief 注销 GM.Watch 指令、清空全部观察分类与屏幕面板并重置全局总开关（模块卸载时对称调用）
     */
    void Shutdown();

    /**
     * @brief 注册或覆盖更新一个调试观察项（同分类同名存在时原地替换 Getter）
     * @param Category 业务分类名称（如 "Player", "Tasks", "Memory", "Net"）
     * @param Name 观察变量名称（如 "Speed", "State", "PendingTasks"）
     * @param Getter 获取当前值格式化字符串的闭包
     * @return 注册或更新成功返回 true；调试开关关闭、名称无效或回调为空时返回 false
     */
    bool Watch(FName Category, FName Name, FValueGetter Getter);

    /**
     * @brief 注销指定分类下的观察项（分类变空时即时移除对应屏幕面板，且默认开启态分类自动回收）
     * @param Category 业务分类名称
     * @param Name 观察变量名称
     * @return 存在并成功移除返回 true
     */
    bool Unwatch(FName Category, FName Name);

    /**
     * @brief 设置指定分类的显示开关（关闭时即时撤销该分类屏幕面板；若分类尚未注册则预创建并记录其启闭状态）
     * @param Category 业务分类名称
     * @param bEnabled 是否显示
     */
    void SetCategoryEnabled(FName Category, bool bEnabled);

    /**
     * @brief 查询指定分类是否处于开启显示态
     * @param Category 业务分类名称
     * @return 分类存在且开启显示返回 true
     */
    [[nodiscard]] bool IsCategoryEnabled(FName Category) const;

    /**
     * @brief 原子设置全局观察显示总开关（关闭时即时撤销全部已注册分类的屏幕面板，线程安全）
     * @param bEnabled 是否全局开启显示
     */
    void SetGlobalEnabled(bool bEnabled);

    /**
     * @brief 原子查询全局观察显示总开关（线程安全）
     * @return 全局开启返回 true
     */
    [[nodiscard]] bool IsGlobalEnabled() const noexcept;

    /**
     * @brief 获取当前注册的所有分类列表
     * @param OutCategories 输出分类名数组
     */
    void GetCategories(TArray<FName>& OutCategories) const;

    /**
     * @brief 获取指定分类下注册的观察项数量
     * @param Category 业务分类名称
     * @return 观察项数量（分类不存在或开关关闭返回 0）
     */
    [[nodiscard]] int32 GetWatchItemCount(FName Category) const;

    /**
     * @brief 清空所有注册的观察项、分类并即时移除对应屏幕面板
     */
    void ClearAll();

    /**
     * @brief 每帧以锁外轻量快照收集已启用分类的实时采样值，并按独立域盐槽位在屏幕原地呈现（驻留时长自适应 DeltaTime 防卡顿闪烁）
     * @param DeltaTime 当前帧流逝秒数
     */
    void TickDisplay(float DeltaTime);

private:
    /**
     * @brief 处理 GM.Watch 控制台命令分发（list | all <on/off> | <Category> <on/off>）
     * @param Args 命令行参数列表
     */
    void HandleWatchCommand(const TArray<FString>& Args);

public:
    /**
     * @brief 获取全局观察中心单例
     * @return 单例引用
     */
    [[nodiscard]] static FGameDebugWatchHub& Get();

private:
    mutable FRWLock RegistryLock;
    TMap<FName, FCategoryGroup> Categories;
    std::atomic<bool> bGlobalEnabled{true};
};

#if GAME_ENABLE_DEBUG_DRAW != 0
    #define GAME_DEBUG_WATCH(Category, Name, Getter) \
        do { \
            (void)FGameDebugWatchHub::Get().Watch(FName(TEXT(Category)), FName(TEXT(Name)), (Getter)); \
        } while (false)

    #define GAME_DEBUG_UNWATCH(Category, Name) \
        do { \
            (void)FGameDebugWatchHub::Get().Unwatch(FName(TEXT(Category)), FName(TEXT(Name))); \
        } while (false)
#else
    #define GAME_DEBUG_WATCH(Category, Name, Getter) ((void)0)
    #define GAME_DEBUG_UNWATCH(Category, Name) ((void)0)
#endif
