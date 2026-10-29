/**
 * @file FSaveGameSegmentRegistry.cpp
 * @brief 存档分段注册与版本迁移管理实现
 */

#include "Save/FSaveGameSegmentRegistry.h"

#include "Logging/GameLogCategories.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

void FSaveGameSegmentRegistry::RegisterSegment(const FGameSaveSegment& Segment) {
    if (Segment.SegmentName.IsNone() || !Segment.Save || !Segment.Load) {
        GAME_LOG(Error, TEXT("【存档子系统】存档段注册被拒：段名、保存委托与加载委托缺一不可。"));
        return;
    }
    Segments.Add(Segment.SegmentName, Segment);
}

bool FSaveGameSegmentRegistry::UnregisterSegment(FName SegmentName) {
    return Segments.Remove(SegmentName) > 0;
}

bool FSaveGameSegmentRegistry::HasSegment(FName SegmentName) const {
    return Segments.Contains(SegmentName);
}

bool FSaveGameSegmentRegistry::BuildArchive(const FString& SlotName, UGameSaveArchive& OutArchive) const {
    OutArchive.Metadata.SlotName = SlotName;
    OutArchive.Metadata.SaveTimestamp = FDateTime::UtcNow();
    OutArchive.Metadata.ArchiveVersion = UGameSaveArchive::CurrentArchiveVersion;
    OutArchive.Segments.Reset();

    if (const TMap<FName, FGameSaveSegmentPayload>* Preserved = PreservedSegmentsBySlot.Find(SlotName)) {
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
            return false;
        }

        OutArchive.Segments.Add(Pair.Key, MoveTemp(Payload));
    }
    return true;
}

bool FSaveGameSegmentRegistry::ApplyArchive(const FString& SlotName, const UGameSaveArchive& Archive) {
    for (const TPair<FName, FGameSaveSegment>& Pair : Segments) {
        const FGameSaveSegmentPayload* Payload = Archive.Segments.Find(Pair.Key);
        if (Payload != nullptr && Payload->SegmentVersion > Pair.Value.SegmentVersion) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段 %s 版本 %d 高于当前段版本 %d，拒绝加载。"),
                *Pair.Key.ToString(), Payload->SegmentVersion, Pair.Value.SegmentVersion);
            return false;
        }
    }

    for (const TPair<FName, FGameSaveSegment>& Pair : Segments) {
        const FGameSaveSegmentPayload* Payload = Archive.Segments.Find(Pair.Key);
        if (Payload == nullptr) {
            GAME_LOG(Warning, TEXT("【存档子系统】存档缺少已注册段，已跳过: %s"), *Pair.Key.ToString());
            continue;
        }

        FMemoryReader Reader(Payload->Bytes, true);
        if (!Pair.Value.Load(Reader, Payload->SegmentVersion) || Reader.IsError()) {
            GAME_LOG(Error, TEXT("【存档子系统】存档段加载失败: %s（落盘版本 %d）"),
                *Pair.Key.ToString(), Payload->SegmentVersion);
            return false;
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
    return true;
}

void FSaveGameSegmentRegistry::ClearPreservedSegmentsForSlot(const FString& SlotName) {
    PreservedSegmentsBySlot.Remove(SlotName);
}

void FSaveGameSegmentRegistry::Reset() {
    Segments.Empty();
    PreservedSegmentsBySlot.Empty();
}

