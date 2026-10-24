// Automatically generated static data hot reload watcher. Do not edit manually.
#pragma once

#include "CoreMinimal.h"

/**
 * @brief 静态数据热更新监听器（静态工具类，编辑器下工作）
 *
 * 设计思路：轮询机制已下沉至 GameCore/IO 的 FDirectoryChangeWatcher（监听目录/扩展名/递归/间隔
 * 全配置驱动），本类仅负责创建该监听器、订阅变更通知并向全部在线世界上下文派发
 * UStaticDataSubsystem::ReloadAllWorldStaticData（PIE 多世界下每个 GameInstance 各持一份数据）；
 * 启停开关沿用 StaticData.HotReload.Enabled，关闭期间冻结观察基线，重新开启后一次性同步。
 */
class GAMEDATASAMPLES_API FStaticDataHotReloadWatcher {
public:
    FStaticDataHotReloadWatcher() = delete;

    /**
     * @brief 启动热更新监听（创建目录监听器并订阅变更通知，重复调用先停旧实例）
     */
    static void Start();

    /**
     * @brief 停止热更新监听（退订并销毁目录监听器）
     */
    static void Stop();
};
