/**
 * @file GamePlayerProfileSaveAdapter.cpp
 * @brief 玩家档案存档适配器实现
 */

#include "Save/GamePlayerProfileSaveAdapter.h"

#include "Dynamic/GamePlayerDataRegistry.h"
#include "Logging/GameLogCategories.h"
#include "Save/USaveGameSubsystem.h"
#include "Serialization/Archive.h"

const FLazyName UGamePlayerProfileSaveAdapter::SegmentName(TEXT("Player.Profile"));

namespace {

/**
 * @brief 序列化或反序列化玩家档案数据字段
 * @param Ar 归档流引用
 * @param Profile 玩家档案结构体引用
 */
void SerializeProfile(FArchive& Ar, FPlayerProfileData& Profile) {
    Ar << Profile.PlayerId
       << Profile.Progression.Level
       << Profile.Progression.Experience
       << Profile.Progression.Gold
       << Profile.ArchiveVersion;
}

}  // namespace

/**
 * @brief 对象销毁生命周期回调：注销档案存档段并释放子系统与注册表引用
 */
void UGamePlayerProfileSaveAdapter::BeginDestroy() {
    Teardown();
    Super::BeginDestroy();
}

/**
 * @brief 绑定存档子系统与玩家数据注册表，并注册档案存档段契约
 * @param InSaveSubsystem 通用存档子系统指针
 * @param InRegistry 玩家数据注册表中枢指针
 */
void UGamePlayerProfileSaveAdapter::Setup(USaveGameSubsystem* InSaveSubsystem, UGamePlayerDataRegistry* InRegistry) {
    Teardown();
    if (InSaveSubsystem == nullptr || InRegistry == nullptr) {
        GAME_LOG(Warning, TEXT("【玩家档案存档适配器】Setup 失败：SaveSubsystem 或 InRegistry 为空。"));
        return;
    }

    SaveSubsystem = InSaveSubsystem;
    DataRegistry = InRegistry;
    TWeakObjectPtr<UGamePlayerDataRegistry> WeakRegistry(InRegistry);

    FGameSaveSegment Segment;
    Segment.SegmentName = SegmentName;
    Segment.SegmentVersion = 1;
    Segment.Save = [WeakRegistry](FArchive& Ar) {
        if (const UGamePlayerDataRegistry* Target = WeakRegistry.Get()) {
            FPlayerProfileData Profile = Target->GetProfile();
            SerializeProfile(Ar, Profile);
        } else {
            Ar.SetError();
        }
    };
    Segment.Load = [WeakRegistry](FArchive& Ar, int32 StoredVersion) {
        if (StoredVersion != 1) {
            return false;
        }
        FPlayerProfileData Profile;
        SerializeProfile(Ar, Profile);
        if (Ar.IsError() || !Ar.AtEnd()) {
            return false;
        }

        UGamePlayerDataRegistry* Target = WeakRegistry.Get();
        if (Target == nullptr) {
            return false;
        }
        Target->SetProfile(Profile);
        return true;
    };

    const TGameStatusResult<void> RegisterResult = InSaveSubsystem->RegisterSegment(Segment);
    if (RegisterResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【玩家档案存档适配器】注册存档段失败: %s"), *RegisterResult.GetError().Message);
        SaveSubsystem.Reset();
        DataRegistry.Reset();
        return;
    }
    GAME_LOG(Log, TEXT("【玩家档案存档适配器】成功建立绑定并注册存档段 [%s]。"), *FName(SegmentName).ToString());
}

/**
 * @brief 手动注销档案存档段并清空引用
 */
void UGamePlayerProfileSaveAdapter::Teardown() {
    if (!IsEngineExitRequested()) {
        if (USaveGameSubsystem* TargetSubsystem = SaveSubsystem.Get()) {
            if (IsValid(TargetSubsystem) && !TargetSubsystem->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) {
                if (TargetSubsystem->UnregisterSegment(SegmentName).IsSuccess()) {
                    GAME_LOG(Log, TEXT("【玩家档案存档适配器】已注销存档段 [%s]。"), *FName(SegmentName).ToString());
                }
            }
        }
    }
    SaveSubsystem.Reset();
    DataRegistry.Reset();
}
