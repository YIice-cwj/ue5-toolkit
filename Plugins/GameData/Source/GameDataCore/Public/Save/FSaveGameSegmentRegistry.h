/**
 * @file FSaveGameSegmentRegistry.h
 * @brief 存档分段注册与版本迁移管理（单一职责 · 负责分段读写委托托管、版本兼容性校验与未知段保留）
 */

#pragma once

#include "CoreMinimal.h"
#include "Save/GameSaveArchive.h"
#include "Types/TGameResult.h"

/**
 * @brief 存档分段注册表
 */
class GAMEDATACORE_API FSaveGameSegmentRegistry {
public:
    FSaveGameSegmentRegistry() = default;

    /**
     * @brief 注册具名存档段契约（包含段名、正整数版本号及读写委托）
     * @param Segment 待注册的存档段契约
     * @return 注册成功返回 Success，段名/版本/委托非法返回 InvalidSegment 失败
     */
    [[nodiscard]] TGameStatusResult<void> RegisterSegment(const FGameSaveSegment& Segment);

    /**
     * @brief 注销指定名称的存档段
     * @param SegmentName 目标存档段名称
     * @return 成功注销返回 Success，未找到指定段返回 SegmentNotFound 失败
     */
    [[nodiscard]] TGameStatusResult<void> UnregisterSegment(FName SegmentName);

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
     * @return 组装成功返回 Success，任一段序列化失败返回 SegmentSaveFailed 失败
     */
    [[nodiscard]] TGameStatusResult<void> BuildArchive(const FString& SlotName, UGameSaveArchive& OutArchive) const;

    /**
     * @brief 预检归档中所有已注册段的版本号是否与当前运行库兼容（只读无副作用，供落盘前校验使用）
     * @param Archive 待校验的归档对象引用
     * @return 全部已注册段版本均不高于当前代码版本返回 Success，否则返回 SegmentVersionMismatch 失败
     */
    [[nodiscard]] TGameStatusResult<void> ValidateArchiveCompatibility(const UGameSaveArchive& Archive) const;

    /**
     * @brief 校验版本并向各已注册段反序列化分发归档数据（未知段自动保留）
     * @param SlotName 存档槽位名称
     * @param Archive 待读取的归档对象引用
     * @return 全部已注册段反序列化成功返回 Success，版本不兼容或损坏返回 Failure
     */
    [[nodiscard]] TGameStatusResult<void> ApplyArchive(const FString& SlotName, const UGameSaveArchive& Archive);

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
