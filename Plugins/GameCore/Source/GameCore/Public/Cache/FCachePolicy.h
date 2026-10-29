/**
 * @file FCachePolicy.h
 * @brief 缓存策略枚举与域策略配置（单一职责 · 负责策略枚举与配置驱动解析）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 缓存生效策略枚举
 */
enum class ECacheStrategy : uint8 {
    /**
     * @brief 自动判定策略（根据实际数据总行数与阈值动态选择 Eager、Lazy 或 LRU）
     */
    Automatic,

    /**
     * @brief 全量预热构建（在初始化或热重载时一次性加载并持有全部条目的只读快照）
     */
    Eager,

    /**
     * @brief 惰性按需构建（初次访问未命中时单条构建，命中后常驻内存，适合超大静态表）
     */
    Lazy,

    /**
     * @brief LRU 限长淘汰（维持固定容量上限，超出后自动淘汰最久未访问条目，适合冷热不均的数据集）
     */
    Lru,
};

/**
 * @brief 域缓存策略配置（从 FConfigRegistry 按命名约定键解析）
 */
struct GAMECORE_API FCachePolicy {
public:
    /**
     * @brief 从 FConfigRegistry 按命名约定键解析指定域的缓存策略
     * @param Domain 缓存业务域名称（对应配置键前缀 Cache.{Domain}.）
     * @return 解析后的域策略配置实例；若对应配置键未注册则回退为默认配置
     */
    static FCachePolicy Resolve(const FName& Domain);

public:
    /**
     * @brief 生效策略（配置为 Automatic 时，将在 Rebuild 阶段根据总行数与阈值动态判定）
     */
    ECacheStrategy Strategy = ECacheStrategy::Automatic;

    /**
     * @brief Automatic 策略判定阈值：行数大于等于该值时启用 Lazy 惰性构建
     */
    int64 LazyMinRows = MAX_int64;

    /**
     * @brief Automatic 策略判定阈值：行数大于等于该值时启用 LRU 策略（优先级高于 Lazy）
     */
    int64 LruMinRows = MAX_int64;

    /**
     * @brief LRU 策略的缓存条目容量上限（条目数超过此值时自动触发淘汰最久未访问节点）
     */
    int64 LruCapacity = 10000;
};
