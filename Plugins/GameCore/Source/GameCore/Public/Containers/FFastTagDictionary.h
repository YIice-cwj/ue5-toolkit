/**
 * @file FFastTagDictionary.h
 * @brief FName 到 64 位 Tag 索引的线程安全双向映射字典
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/FFastTagBitmask.h"
#include "HAL/CriticalSection.h"

/**
 * @brief FName 到 64 位 Tag 索引的线程安全双向映射字典（读写锁分离保护高频只读查询）
 */
class GAMECORE_API FFastTagDictionary {
public:
    static constexpr int32 MaxTagCount = 64;

    /**
     * @brief 获取全局单例字典实例
     * @return 字典单例引用
     */
    static FFastTagDictionary& Get();

    /**
     * @brief 注册或查询指定标签名称的位索引
     * @param TagName 标签名称
     * @return 分配的位索引 [0, 63]，注册满或非法名称返回 INDEX_NONE
     */
    int32 RegisterOrFindTag(FName TagName);

    /**
     * @brief 查询已知标签名称的位索引
     * @param TagName 标签名称
     * @return 索引值，未注册返回 INDEX_NONE
     */
    [[nodiscard]] int32 FindTagIndex(FName TagName) const;

    /**
     * @brief 查询已知位索引对应的标签名称
     * @param BitIndex 索引值
     * @return 注册的名称，越界或未注册返回 NAME_None
     */
    [[nodiscard]] FName FindTagName(int32 BitIndex) const;

    /**
     * @brief 批量将标签名称列表转换为位掩码（单次加锁批量处理）
     * @param TagNames 标签名称视图
     * @param OutAllSucceeded 可选输出标志：若所有标签均合法且未超出 64 上限则置为 true，否则置为 false
     * @return 聚合位掩码
     */
    [[nodiscard]] FFastTagBitmask MakeMask(TConstArrayView<FName> TagNames, bool* OutAllSucceeded = nullptr);

    /**
     * @brief 获取当前已注册的标签总数
     * @return 标签数量
     */
    [[nodiscard]] int32 Num() const;

    /**
     * @brief 重置清空所有注册信息（仅限单测或环境重置使用）
     */
    void Reset();

private:
    /**
     * @brief 在已持有写锁的前提下注册或查询指定标签名称的位索引
     * @param TagName 标签名称
     * @return 分配的位索引 [0, 63]，注册满或非法名称返回 INDEX_NONE
     */
    int32 RegisterOrFindTagUnderWriteLock(FName TagName);

private:
    mutable FRWLock RwLock;
    TArray<FName> RegisteredTagNames;
    TMap<FName, int32> TagNameToIndexMap;
};
