/**
 * @file URandomSubsystem.h
 * @brief 世界级多通道隔离随机数子系统（按通道名隔离随机流并支持归档序列化）
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Random/FGameRandomManager.h"
#include "Random/FGameRandomStream.h"

#include "URandomSubsystem.generated.h"

/**
 * @brief 世界作用域多通道隔离随机数管理子系统（引擎外壳 Thin-Shell，核心算法委托给 FGameRandomManager）
 */
UCLASS()
class GAMECORE_API URandomSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    static constexpr uint32 ARCHIVE_MAGIC = FGameRandomManager::ARCHIVE_MAGIC;
    static constexpr uint32 ARCHIVE_VERSION = FGameRandomManager::ARCHIVE_VERSION;
    static constexpr int32 MAX_IMPORT_STREAMS = FGameRandomManager::MAX_IMPORT_STREAMS;

public:
    /**
     * @brief 世界销毁生命周期回调：清空所有通道随机流
     */
    void Deinitialize() override;

    /**
     * @brief 获取或惰性创建指定通道的独立随机流（首次创建按通道名稳定 CRC32 派生种子）
     * @param ChannelName 业务通道名（如 Combat / Loot / AI / Cosmetic；None 自动归入 Default）
     * @return 对应通道的隔离随机流引用
     */
    [[nodiscard]] FGameRandomStream& GetOrCreateStream(const FName& ChannelName);

    /**
     * @brief 获取或惰性创建指定通道的独立随机流共享引用句柄（跨通道移除与重载生命周期安全）
     * @param ChannelName 业务通道名（None 自动归入 Default）
     * @return 随机流共享引用句柄
     */
    [[nodiscard]] TSharedRef<FGameRandomStream> GetOrCreateStreamShared(const FName& ChannelName);

    /**
     * @brief 使用显式 64 位种子初始化（或重置）指定通道的随机流
     * @param ChannelName 业务通道名
     * @param Seed 显式 64 位初始种子
     * @return 初始化后的通道随机流引用
     */
    FGameRandomStream& InitializeStream(const FName& ChannelName, uint64 Seed);

    /**
     * @brief 检查指定通道是否已注册存在
     * @param ChannelName 业务通道名
     * @return 已存在返回 true
     */
    [[nodiscard]] bool HasStream(const FName& ChannelName) const;

    /**
     * @brief 移除指定通道的随机流
     * @param ChannelName 业务通道名
     * @return 原本存在并成功移除返回 true
     */
    bool RemoveStream(const FName& ChannelName);

    /**
     * @brief 清空当前世界内注册的全部随机流通道
     */
    void ClearAllStreams();

    /**
     * @brief 获取当前已创建的独立随机流通道数量
     * @return 通道总数
     */
    [[nodiscard]] int32 GetStreamCount() const noexcept {
        return Manager.GetStreamCount();
    }

    /**
     * @brief 获取当前所有已注册的通道名称列表
     * @param OutChannelNames 接收通道名称的输出数组
     * @return 已注册通道数量
     */
    int32 GetChannelNames(TArray<FName>& OutChannelNames) const {
        return Manager.GetChannelNames(OutChannelNames);
    }

    /**
     * @brief 获取纯 C++ 随机数管理器核心
     * @return 管理器引用
     */
    [[nodiscard]] FGameRandomManager& GetManager() noexcept {
        return Manager;
    }

    /**
     * @brief 获取纯 C++ 随机数管理器核心（常量版本）
     * @return 管理器常量引用
     */
    [[nodiscard]] const FGameRandomManager& GetManager() const noexcept {
        return Manager;
    }

    /**
     * @brief 将所有通道随机流内部状态按确定性字典序导出至二进制归档
     * @param Ar 写入归档器
     */
    void ExportStreams(FArchive& Ar) const;

    /**
     * @brief 从二进制归档原子校验并恢复所有通道随机流状态（校验失败不破坏既有状态）
     * @param Ar 读取归档器
     * @return 魔数、版本与条目完整校验通过返回 true
     */
    bool ImportStreams(FArchive& Ar);

public:
    /**
     * @brief 获取随机子系统引用（要求 WorldContextObject 必须关联有效 UWorld，否则触发 checkf；不确定有效时请用 GetPtr）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统引用
     */
    static URandomSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取随机子系统指针（上下文无效返回 nullptr）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统指针
     */
    static URandomSubsystem* GetPtr(const UObject* WorldContextObject);

private:
    FGameRandomManager Manager;
};
