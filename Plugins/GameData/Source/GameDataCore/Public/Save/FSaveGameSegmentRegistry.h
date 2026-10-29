/**
 * @file FSaveGameSegmentRegistry.h
 * @brief 存档分段注册与版本迁移管理（单一职责 · 负责分段读写委托托管与未知段保留）
 */

#pragma once

#include "CoreMinimal.h"
#include "Save/GameSaveArchive.h"

/**
 * @brief 存档分段注册表
 */
class GAMEDATACORE_API FSaveGameSegmentRegistry {
public:
    FSaveGameSegmentRegistry() = default;

    /**
     * @brief 注册具名存档段契约（包含段名、当前版本及读写委托）
     * @param Segment 待注册的存档段契约
     */
    void RegisterSegment(const FGameSaveSegment& Segment);

    /**
     * @brief 注销指定名称的存档段
     * @param SegmentName 目标存档段名称
     * @return 若成功注销返回 true，未找到指定段返回 false
     */
    bool UnregisterSegment(FName SegmentName);

    /**
     * @brief 检查指定名称的存档段是否已注册
     * @param SegmentName 目标存档段名称
     * @return 已注册返回 true，否则返回 false
     */
    [[nodiscard]] bool HasSegment(FName SegmentName) const;

    /**
     * @brief 驱动所有已注册段序列化数据，组装为目标归档对象
     * @param SlotName 存档槽位名称
     * @param OutArchive 输出的归档对象引用
     * @return 组装成功返回 true，发生异常返回 false
     */
    bool BuildArchive(const FString& SlotName, UGameSaveArchive& OutArchive) const;

    /**
     * @brief 校验版本并向各已注册段反序列化分发归档数据（未知段自动保留）
     * @param SlotName 存档槽位名称
     * @param Archive 待读取的归档对象引用
     * @return 全部已注册段反序列化成功返回 true，版本不兼容或损坏返回 false
     */
    bool ApplyArchive(const FString& SlotName, const UGameSaveArchive& Archive);

    /**
     * @brief 移除并清理指定槽位缓存的未知保留段数据
     * @param SlotName 存档槽位名称
     */
    void ClearPreservedSegmentsForSlot(const FString& SlotName);

    /**
     * @brief 清空注册表中所有的注册段契约与槽位保留段缓存
     */
    void Reset();

private:
    TMap<FName, FGameSaveSegment> Segments;
    TMap<FString, TMap<FName, FGameSaveSegmentPayload>> PreservedSegmentsBySlot;
};
