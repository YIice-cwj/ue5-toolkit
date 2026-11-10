/**
 * @file FGameDebugPrint.h
 * @brief 现代化屏幕与通道调试打印工具（支持固定通道原地覆盖防刷屏与 Shipping 构建零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 屏幕调试信息打印工具类
 */
class GAMEDEBUG_API FGameDebugPrint {
public:
    /**
     * @brief 向屏幕输出调试消息
     * @param Message 消息文本
     * @param Color 字体颜色（默认青色）
     * @param Duration 屏幕驻留时长（秒）
     * @param Key 覆盖槽位 Key（默认 -1 代表追加新行，非 -1 原地刷新）
     */
    static void Print(
        const FString& Message,
        const FColor& Color = FColor::Cyan,
        float Duration = 2.0f,
        uint64 Key = static_cast<uint64>(-1)
    );

    /**
     * @brief 基于通道名称向屏幕输出调试消息（自动根据通道哈希生成固定 Key，原地刷新防刷屏）
     * @param Channel 通道唯一名称（如 "Combat", "Network", "Inventory"）
     * @param Message 消息文本
     * @param Color 字体颜色（默认黄色）
     * @param Duration 屏幕驻留时长（秒）
     */
    static void PrintChannel(
        const FName& Channel,
        const FString& Message,
        const FColor& Color = FColor::Yellow,
        float Duration = 2.0f
    );

    /**
     * @brief 向屏幕输出高亮警告消息（橙色，默认驻留 5 秒）
     * @param Message 警告文本
     * @param Duration 驻留时长
     */
    static void PrintWarning(const FString& Message, float Duration = 5.0f);

    /**
     * @brief 向屏幕输出高亮错误消息（红色，默认驻留 5 秒）
     * @param Message 错误文本
     * @param Duration 驻留时长
     */
    static void PrintError(const FString& Message, float Duration = 5.0f);

    /**
     * @brief 计算通道名称对应的屏幕槽位 Key
     * @param Channel 通道名称
     * @return 64 位槽位 Key
     */
    [[nodiscard]] static uint64 GetChannelKey(const FName& Channel);
};

#if !UE_BUILD_SHIPPING
    #define GAME_DEBUG_PRINT(Msg, ...) \
        FGameDebugPrint::Print(FString::Printf(TEXT(Msg), ##__VA_ARGS__))

    #define GAME_DEBUG_PRINT_COLOR(Color, Duration, Msg, ...) \
        FGameDebugPrint::Print(FString::Printf(TEXT(Msg), ##__VA_ARGS__), Color, Duration)

    #define GAME_DEBUG_PRINT_CHANNEL(Channel, Msg, ...) \
        FGameDebugPrint::PrintChannel(Channel, FString::Printf(TEXT(Msg), ##__VA_ARGS__))

    #define GAME_DEBUG_PRINT_WARN(Msg, ...) \
        FGameDebugPrint::PrintWarning(FString::Printf(TEXT(Msg), ##__VA_ARGS__))

    #define GAME_DEBUG_PRINT_ERROR(Msg, ...) \
        FGameDebugPrint::PrintError(FString::Printf(TEXT(Msg), ##__VA_ARGS__))
#else
    #define GAME_DEBUG_PRINT(Msg, ...) ((void)0)
    #define GAME_DEBUG_PRINT_COLOR(Color, Duration, Msg, ...) ((void)0)
    #define GAME_DEBUG_PRINT_CHANNEL(Channel, Msg, ...) ((void)0)
    #define GAME_DEBUG_PRINT_WARN(Msg, ...) ((void)0)
    #define GAME_DEBUG_PRINT_ERROR(Msg, ...) ((void)0)
#endif
