/**
 * @file FGameRandomManager.h
 * @brief 纯 C++ 多通道隔离随机数流管理器（机制与引擎外壳彻底分离）
 */

#pragma once

#include "CoreMinimal.h"
#include "Random/FGameRandomStream.h"

/**
 * @brief 多通道隔离随机数管理器核心（零 UWorld 依赖，可在任何多线程、独立离线工具或纯 C++ 单测中独立运行）
 */
class GAMECORE_API FGameRandomManager {
public:
    static constexpr uint32 ARCHIVE_MAGIC = 0x47524E47u;
    static constexpr uint32 ARCHIVE_VERSION = 1u;
    static constexpr int32 MAX_IMPORT_STREAMS = 4096;

public:
    FGameRandomManager() = default;
    ~FGameRandomManager() = default;

    /**
     * @brief 深拷贝构造函数（克隆各通道独立随机流状态，互不共享底层实例）
     * @param Other 源管理器
     */
    FGameRandomManager(const FGameRandomManager& Other);

    /**
     * @brief 深拷贝赋值运算符（克隆各通道独立随机流状态，互不共享底层实例）
     * @param Other 源管理器
     * @return 当前管理器引用
     */
    FGameRandomManager& operator=(const FGameRandomManager& Other);

    FGameRandomManager(FGameRandomManager&&) noexcept = default;
    FGameRandomManager& operator=(FGameRandomManager&&) noexcept = default;

    /**
     * @brief 获取或惰性创建指定通道的独立随机流（底层节点堆地址在新增通道扩容时保持稳定；注意调用 RemoveStream 或 ClearAllStreams 移除对应通道后该裸引用将失效，跨移除周期请使用 GetOrCreateStreamShared）
     * @param ChannelName 业务通道名（None 自动归入 Default）
     * @return 随机流引用
     */
    [[nodiscard]] FGameRandomStream& GetOrCreateStream(const FName& ChannelName);

    /**
     * @brief 获取或惰性创建指定通道的独立随机流共享引用句柄（即使通道被 RemoveStream 移除仍保持内存安全）
     * @param ChannelName 业务通道名（None 自动归入 Default）
     * @return 随机流共享引用句柄
     */
    [[nodiscard]] TSharedRef<FGameRandomStream> GetOrCreateStreamShared(const FName& ChannelName);

    /**
     * @brief 使用显式种子初始化（或重置）指定通道的随机流（底层节点原地重置，不会使既有通道引用悬垂）
     * @param ChannelName 业务通道名
     * @param Seed 64 位初始种子
     * @return 随机流引用
     */
    FGameRandomStream& InitializeStream(const FName& ChannelName, uint64 Seed);

    /**
     * @brief 检查指定通道是否已存在
     * @param ChannelName 业务通道名
     * @return 存在返回 true
     */
    [[nodiscard]] bool HasStream(const FName& ChannelName) const;

    /**
     * @brief 移除指定通道的随机流（注意：移除后此前通过 GetOrCreateStream 获取的该通道裸引用将失效）
     * @param ChannelName 业务通道名
     * @return 存在并成功移除返回 true
     */
    bool RemoveStream(const FName& ChannelName);

    /**
     * @brief 清空全部已注册的随机流通道（注意：清空后此前通过 GetOrCreateStream 获取的所有裸引用均将失效）
     */
    void ClearAllStreams();

    /**
     * @brief 查询当前已创建的随机流通道总数
     * @return 通道总数
     */
    [[nodiscard]] int32 GetStreamCount() const noexcept {
        return ChannelStreams.Num();
    }

    /**
     * @brief 获取当前所有已注册的通道名称列表
     * @param OutChannelNames 接收通道名称的输出数组
     * @return 已注册通道数量
     */
    int32 GetChannelNames(TArray<FName>& OutChannelNames) const {
        ChannelStreams.GenerateKeyArray(OutChannelNames);
        return OutChannelNames.Num();
    }

    /**
     * @brief 将所有通道随机流内部状态按确定性字典序导出至二进制归档
     * @param Ar 写入归档器
     */
    void ExportStreams(FArchive& Ar) const;

    /**
     * @brief 从二进制归档原子校验并恢复所有通道随机流状态（重复通道或格式非法时原子回滚）
     * @param Ar 读取归档器
     * @return 校验并恢复成功返回 true
     */
    bool ImportStreams(FArchive& Ar);

    /**
     * @brief 规范化通道名称
     * @param ChannelName 待规范化通道名称
     * @return 规范化后的通道名称
     */
    [[nodiscard]] static FName NormalizeChannelName(const FName& ChannelName) noexcept;

    /**
     * @brief 基于规范化通道名（统一转小写）派生跨进程一致的 64 位确定性初始种子
     * @param NormalizedName 规范化名称
     * @return 64 位初始种子
     */
    [[nodiscard]] static uint64 DeriveChannelDefaultSeed(const FName& NormalizedName);

private:
    TMap<FName, TSharedRef<FGameRandomStream>> ChannelStreams;
};
