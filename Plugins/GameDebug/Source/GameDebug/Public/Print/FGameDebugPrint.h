/**
 * @file FGameDebugPrint.h
 * @brief 现代化屏幕与通道调试打印工具（支持独立域盐通道原地覆盖防刷屏与 Shipping 构建零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"
#include "Config/GameCompileSwitches.h"

/**
 * @brief 屏幕调试信息打印工具类（纯静态不可实例化，仅负责屏幕输出，结构化日志请使用 GameLogging）
 */
class GAMEDEBUG_API FGameDebugPrint final {
public:
    static constexpr uint64 InvalidKey = MAX_uint64;

public:
    FGameDebugPrint() = delete;
    ~FGameDebugPrint() = delete;

    FGameDebugPrint(const FGameDebugPrint&) = delete;
    FGameDebugPrint& operator=(const FGameDebugPrint&) = delete;
    FGameDebugPrint(FGameDebugPrint&&) = delete;
    FGameDebugPrint& operator=(FGameDebugPrint&&) = delete;

    /**
     * @brief 向屏幕输出调试消息（负 Duration 会自动钳制为 0.0f）
     * @param Message 消息文本
     * @param Color 字体颜色（默认青色）
     * @param Duration 屏幕驻留时长（秒）
     * @param Key 覆盖槽位 Key（默认 InvalidKey 代表追加新行，其余值原地刷新）
     */
    static void Print(
        const FString& Message,
        const FColor& Color = FColor::Cyan,
        float Duration = 2.0f,
        uint64 Key = InvalidKey
    );

    /**
     * @brief 基于通道名称向屏幕输出调试消息（自动经 64 位独立域盐哈希生成固定 Key，原地刷新防刷屏且不与 WatchHub 冲突）
     * @param Channel 通道唯一名称（如 "Combat", "Network", "Inventory"）
     * @param Message 消息文本
     * @param Color 字体颜色（默认黄色）
     * @param Duration 屏幕驻留时长（秒）
     */
    static void PrintChannel(
        FName Channel,
        const FString& Message,
        const FColor& Color = FColor::Yellow,
        float Duration = 2.0f
    );

    /**
     * @brief 向屏幕输出高亮警告消息（橙色，默认驻留 5 秒）
     * @param Message 警告文本
     * @param Duration 驻留时长（秒）
     */
    static void PrintWarning(const FString& Message, float Duration = 5.0f);

    /**
     * @brief 向屏幕输出高亮错误消息（红色，默认驻留 5 秒）
     * @param Message 错误文本
     * @param Duration 驻留时长（秒）
     */
    static void PrintError(const FString& Message, float Duration = 5.0f);

    /**
     * @brief 计算通道名称对应的 64 位域隔离屏幕槽位 Key
     * @param Channel 通道名称
     * @return 64 位槽位 Key（NAME_None 返回 InvalidKey，其余永不等于 InvalidKey）
     */
    [[nodiscard]] static uint64 GetChannelKey(FName Channel) noexcept;
};

#if GAME_ENABLE_DEBUG_DRAW != 0
    #define GAME_DEBUG_PRINT(Msg, ...) \
        do { \
            FGameDebugPrint::Print(FString::Printf(TEXT(Msg), ##__VA_ARGS__)); \
        } while (false)

    #define GAME_DEBUG_PRINT_COLOR(Color, Duration, Msg, ...) \
        do { \
            FGameDebugPrint::Print(FString::Printf(TEXT(Msg), ##__VA_ARGS__), (Color), (Duration)); \
        } while (false)

    #define GAME_DEBUG_PRINT_CHANNEL(Channel, Msg, ...) \
        do { \
            FGameDebugPrint::PrintChannel((Channel), FString::Printf(TEXT(Msg), ##__VA_ARGS__)); \
        } while (false)

    #define GAME_DEBUG_PRINT_WARN(Msg, ...) \
        do { \
            FGameDebugPrint::PrintWarning(FString::Printf(TEXT(Msg), ##__VA_ARGS__)); \
        } while (false)

    #define GAME_DEBUG_PRINT_ERROR(Msg, ...) \
        do { \
            FGameDebugPrint::PrintError(FString::Printf(TEXT(Msg), ##__VA_ARGS__)); \
        } while (false)
#else
    #define GAME_DEBUG_PRINT(Msg, ...) ((void)0)
    #define GAME_DEBUG_PRINT_COLOR(Color, Duration, Msg, ...) ((void)0)
    #define GAME_DEBUG_PRINT_CHANNEL(Channel, Msg, ...) ((void)0)
    #define GAME_DEBUG_PRINT_WARN(Msg, ...) ((void)0)
    #define GAME_DEBUG_PRINT_ERROR(Msg, ...) ((void)0)
#endif
