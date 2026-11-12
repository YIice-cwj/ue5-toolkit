/**
 * @file URandomSubsystem.cpp
 * @brief URandomSubsystem 世界级多通道隔离随机数子系统薄壳实现
 */

#include "Random/URandomSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

/**
 * @brief 世界销毁生命周期回调：清空所有通道随机流
 */
void URandomSubsystem::Deinitialize() {
    Manager.ClearAllStreams();
    Super::Deinitialize();
}

/**
 * @brief 获取或惰性创建指定通道的独立随机流
 * @param ChannelName 业务通道名
 * @return 对应通道的隔离随机流引用
 */
FGameRandomStream& URandomSubsystem::GetOrCreateStream(const FName& ChannelName) {
    return Manager.GetOrCreateStream(ChannelName);
}

/**
 * @brief 获取或惰性创建指定通道的独立随机流共享引用句柄
 * @param ChannelName 业务通道名
 * @return 随机流共享引用句柄
 */
TSharedRef<FGameRandomStream> URandomSubsystem::GetOrCreateStreamShared(const FName& ChannelName) {
    return Manager.GetOrCreateStreamShared(ChannelName);
}

/**
 * @brief 使用显式 64 位种子初始化（或重置）指定通道的随机流
 * @param ChannelName 业务通道名
 * @param Seed 显式 64 位初始种子
 * @return 初始化后的通道随机流引用
 */
FGameRandomStream& URandomSubsystem::InitializeStream(const FName& ChannelName, uint64 Seed) {
    return Manager.InitializeStream(ChannelName, Seed);
}

/**
 * @brief 检查指定通道是否已注册存在
 * @param ChannelName 业务通道名
 * @return 已存在返回 true
 */
bool URandomSubsystem::HasStream(const FName& ChannelName) const {
    return Manager.HasStream(ChannelName);
}

/**
 * @brief 移除指定通道的随机流
 * @param ChannelName 业务通道名
 * @return 原本存在并成功移除返回 true
 */
bool URandomSubsystem::RemoveStream(const FName& ChannelName) {
    return Manager.RemoveStream(ChannelName);
}

/**
 * @brief 清空当前世界内注册的全部随机流通道
 */
void URandomSubsystem::ClearAllStreams() {
    Manager.ClearAllStreams();
}

/**
 * @brief 将所有通道随机流内部状态按确定性字典序导出至二进制归档
 * @param Ar 写入归档器
 */
void URandomSubsystem::ExportStreams(FArchive& Ar) const {
    Manager.ExportStreams(Ar);
}

/**
 * @brief 从二进制归档原子校验并恢复所有通道随机流状态
 * @param Ar 读取归档器
 * @return 恢复成功返回 true
 */
bool URandomSubsystem::ImportStreams(FArchive& Ar) {
    return Manager.ImportStreams(Ar);
}

/**
 * @brief 获取随机子系统引用
 * @param WorldContextObject 世界上下文对象
 * @return 子系统引用
 */
URandomSubsystem& URandomSubsystem::Get(const UObject* WorldContextObject) {
    URandomSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 安全获取随机子系统指针
 * @param WorldContextObject 世界上下文对象
 * @return 子系统指针，无效返回 nullptr
 */
URandomSubsystem* URandomSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<URandomSubsystem>();
    }
    return nullptr;
}
