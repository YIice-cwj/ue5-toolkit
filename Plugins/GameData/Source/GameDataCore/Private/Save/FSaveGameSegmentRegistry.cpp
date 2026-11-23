/**
 * @file FSaveGameSegmentRegistry.cpp
 * @brief 存档分段注册与版本迁移管理实现
 */

#include "Save/FSaveGameSegmentRegistry.h"

#include "Core/DataErrorCodes.h"
#include "Logging/GameLogCategories.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

/**
 * @brief 注册具名存档段契约（包含段名、正整数版本号及读写委托）
 * @param Segment 待注册的存档段契约
 * @return 注册成功返回 Success，段名/版本/委托非法返回 InvalidSegment 失败
 */
TGameStatusResult<void> FSaveGameSegmentRegistry::RegisterSegment(const FGameSaveSegment& Segment) {
    if (Segment.SegmentName.IsNone() || Segment.SegmentVersion <= 0 || !Segment.Save || !Segment.Load) {
        GAME_LOG(Error, TEXT("【存档子系统】存档段注册被拒：段名、正整数版本号、保存委托与加载委托缺一不可。"));
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSegment,
            TEXT("存档段注册参数无效：段名、正整数版本号与读写委托均须有效")
        );
    }
    Segments.Add(Segment.SegmentName, Segment);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 注销指定名称的存档段
 * @param SegmentName 目标存档段名称
 * @return 成功注销返回 Success，未找到指定段返回 SegmentNotFound 失败
 */
TGameStatusResult<void> FSaveGameSegmentRegistry::UnregisterSegment(FName SegmentName) {
    if (Segments.Remove(SegmentName) > 0) {
        return TGameStatusResult<void>::Success();
    }
    return TGameStatusResult<void>::Failure(
        Game::ModuleId::Data,
        Game::DataErrorCodes::SegmentNotFound,
        FString::Printf(TEXT("未找到指定的存档段: %s"), *SegmentName.ToString())
    );
}

/**
 * @brief 检查指定名称的存档段是否已注册
 * @param SegmentName 目标存档段名称
 * @return 已注册返回 true，否则返回 false
 */
bool FSaveGameSegmentRegistry::HasSegment(FName SegmentName) const {
    return Segments.Contains(SegmentName);
}

/**
 * @brief 驱动所有已注册段序列化数据，组装为目标归档对象
 * @param SlotName 存档槽位名称
 * @param OutArchive 输出的归档对象引用
 * @return 组装成功返回 Success，任一段序列化失败返回 SegmentSaveFailed 失败
 */
TGameStatusResult<void> FSaveGameSegmentRegistry::BuildArchive(const FString& SlotName, UGameSaveArchive& OutArchive) const {
    OutArchive.Metadata.SlotName = SlotName;
    OutArchive.Metadata.SaveTimestamp = FDateTime::UtcNow();
    OutArchive.Metadata.ArchiveVersion = UGameSaveArchive::CurrentArchiveVersion;
    OutArchive.Segments.Reset();

    const TMap<FName, FGameSaveSegmentPayload>* Preserved = PreservedSegmentsBySlot.Find(SlotName);
    const int32 EstimatedSegmentCount = Segments.Num() + (Preserved != nullptr ? Preserved->Num() : 0);
    OutArchive.Segments.Reserve(EstimatedSegmentCount);

    if (Preserved != nullptr) {
        for (const TPair<FName, FGameSaveSegmentPayload>& Pair : *Preserved) {
            if (!Segments.Contains(Pair.Key)) {
                OutArchive.Segments.Add(Pair.Key, Pair.Value);
            }
        }
    }

    for (const TPair<FName, FGameSaveSegment>& Pair : Segments) {
        FGameSaveSegmentPayload Payload;
        Payload.SegmentVersion = Pair.Value.SegmentVersion;

        FMemoryWriter Writer(Payload.Bytes, true);
        Pair.Value.Save(Writer);
        if (Writer.IsError()) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段保存失败: %s"), *Pair.Key.ToString());
            return TGameStatusResult<void>::Failure(
                Game::ModuleId::Data,
                Game::DataErrorCodes::SegmentSaveFailed,
                FString::Printf(TEXT("保存存档段失败: %s"), *Pair.Key.ToString())
            );
        }

        OutArchive.Segments.Add(Pair.Key, MoveTemp(Payload));
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 预检归档中所有已注册段的版本号是否与当前运行库兼容（只读无副作用，供落盘前校验使用）
 * @param Archive 待校验的归档对象引用
 * @return 全部已注册段版本均不高于当前代码版本返回 Success，否则返回 SegmentVersionMismatch 失败
 */
TGameStatusResult<void> FSaveGameSegmentRegistry::ValidateArchiveCompatibility(const UGameSaveArchive& Archive) const {
    for (const TPair<FName, FGameSaveSegment>& Pair : Segments) {
        const FGameSaveSegmentPayload* Payload = Archive.Segments.Find(Pair.Key);
        if (Payload == nullptr) {
            continue;
        }
        if (Payload->SegmentVersion <= 0) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段 %s 版本 %d 非法（须为正整数），拒绝加载。"),
                *Pair.Key.ToString(), Payload->SegmentVersion);
            return TGameStatusResult<void>::Failure(
                Game::ModuleId::Data,
                Game::DataErrorCodes::SegmentVersionMismatch,
                FString::Printf(
                    TEXT("存档段 %s 存储版本 %d 非法（须为正整数）"),
                    *Pair.Key.ToString(),
                    Payload->SegmentVersion
                )
            );
        }
        if (Payload->SegmentVersion > Pair.Value.SegmentVersion) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段 %s 版本 %d 高于当前段版本 %d，拒绝加载。"),
                *Pair.Key.ToString(), Payload->SegmentVersion, Pair.Value.SegmentVersion);
            return TGameStatusResult<void>::Failure(
                Game::ModuleId::Data,
                Game::DataErrorCodes::SegmentVersionMismatch,
                FString::Printf(
                    TEXT("存档段 %s 存储版本 %d 高于当前版本 %d"),
                    *Pair.Key.ToString(),
                    Payload->SegmentVersion,
                    Pair.Value.SegmentVersion
                )
            );
        }
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 校验版本并向各已注册段反序列化分发归档数据（未知段自动保留）
 * @param SlotName 存档槽位名称
 * @param Archive 待读取的归档对象引用
 * @return 全部已注册段反序列化成功返回 Success，版本不兼容或损坏返回 Failure
 */
TGameStatusResult<void> FSaveGameSegmentRegistry::ApplyArchive(const FString& SlotName, const UGameSaveArchive& Archive) {
    GAME_TRY(ValidateArchiveCompatibility(Archive));

    for (const TPair<FName, FGameSaveSegment>& Pair : Segments) {
        const FGameSaveSegmentPayload* Payload = Archive.Segments.Find(Pair.Key);
        if (Payload == nullptr) {
            GAME_LOG(Warning, TEXT("【存档子系统】存档缺少已注册段，已跳过: %s"), *Pair.Key.ToString());
            continue;
        }

        FMemoryReader Reader(Payload->Bytes, true);
        Reader.ArMaxSerializeSize = FMath::Max<int64>(1, Payload->Bytes.Num());
        if (!Pair.Value.Load(Reader, Payload->SegmentVersion) || Reader.IsError()) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段加载失败: %s（落盘版本 %d）"),
                *Pair.Key.ToString(), Payload->SegmentVersion);
            return TGameStatusResult<void>::Failure(
                Game::ModuleId::Data,
                Game::DataErrorCodes::SegmentLoadFailed,
                FString::Printf(TEXT("加载存档段失败: %s (版本 %d)"), *Pair.Key.ToString(), Payload->SegmentVersion)
            );
        }
    }

    const FString TargetSlotKey = !SlotName.IsEmpty() ? SlotName : Archive.Metadata.SlotName;
    if (!TargetSlotKey.IsEmpty()) {
        TMap<FName, FGameSaveSegmentPayload>& Preserved = PreservedSegmentsBySlot.FindOrAdd(TargetSlotKey);
        Preserved.Reset();
        for (const TPair<FName, FGameSaveSegmentPayload>& Pair : Archive.Segments) {
            if (!Segments.Contains(Pair.Key)) {
                Preserved.Add(Pair.Key, Pair.Value);
            }
        }
        if (Preserved.IsEmpty()) {
            PreservedSegmentsBySlot.Remove(TargetSlotKey);
        }
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 移除并清理指定槽位缓存的未知保留段数据
 * @param SlotName 存档槽位名称
 */
void FSaveGameSegmentRegistry::ClearPreservedSegmentsForSlot(const FString& SlotName) {
    PreservedSegmentsBySlot.Remove(SlotName);
}

/**
 * @brief 清空注册表中所有的注册段契约与槽位保留段缓存
 */
void FSaveGameSegmentRegistry::Reset() {
    Segments.Empty();
    PreservedSegmentsBySlot.Empty();
}
