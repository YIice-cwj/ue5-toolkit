/**
 * @file FConfigSourceBridge.h
 * @brief 外部配置源（INI 节与命令行参数）向 FConfigRegistry 的强类型安全注入桥接器
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 外部文本配置源桥接工具类（自动对齐已注册变体类型、支持显式类型前缀并拦截只读键）
 */
class GAMECORE_API FConfigSourceBridge {
public:
    FConfigSourceBridge() = delete;

    /**
     * @brief 从指定 INI 配置文件的指定节读取全部键值对并注入 FConfigRegistry
     * @param ConfigFilename 目标 INI 文件名（如 GGameIni）
     * @param SectionName 目标节名称（如 TEXT("GameCore.Config")）
     * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
     * @return 成功解析并写入的配置项数量
     */
    static int32 LoadIniSection(const FString& ConfigFilename, const FString& SectionName, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 批量解析内存键值对映射并注入 FConfigRegistry
     * @param KeyValues 键值对集合（键支持可选前缀如 int32:MaxPlayers、float:Speed 等）
     * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
     * @return 成功解析并写入的配置项数量
     */
    static int32 ApplyKeyValueMap(const TMap<FString, FString>& KeyValues, const UObject* WorldContextObject = nullptr);

    /**
     * @brief 解析命令行参数中带指定前缀的覆盖项（如 -Config.Combat.Crit=0.5）并注入 FConfigRegistry
     * @param CommandLine 原始命令行字符串
     * @param ArgPrefix 命令行参数匹配前缀（默认 TEXT("Config.")，自动兼容前导 -）
     * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
     * @return 成功解析并写入的配置项数量
     */
    static int32 ApplyCommandLineOverrides(
        const TCHAR* CommandLine,
        const FString& ArgPrefix = TEXT("Config."),
        const UObject* WorldContextObject = nullptr);

    /**
     * @brief 解析单条外部键值规格并安全写入 FConfigRegistry（只读键与非法格式在边界直接拦截）
     * @param RawKeySpec 原始键规格（如 TEXT("Combat.Crit") 或 TEXT("float:Combat.Crit")）
     * @param RawValue 原始字符串值
     * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
     * @return 解析合法且成功写入返回 true；只读拦截或格式非法返回 false
     */
    static bool ApplySingleEntry(const FString& RawKeySpec, const FString& RawValue, const UObject* WorldContextObject = nullptr);
};
