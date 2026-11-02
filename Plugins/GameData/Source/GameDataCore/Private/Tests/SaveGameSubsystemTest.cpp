/**
 * @file SaveGameSubsystemTest.cpp
 * @brief 存档子系统自动化测试（段迁移、代次防竞争、最新异步加载与容灾恢复），Shipping 构建自动剔除
 */

#include "Save/USaveGameSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Tests/GameSaveTestTypes.h"

/**
 * @brief 记录一次异步加载完成结果
 * @param SlotName 完成回调对应的槽位名称
 * @param bSuccess 加载是否成功
 */
void UGameSaveTestEventListener::RecordLoadComplete(const FString&, bool bSuccess) {
    CompletionResults.Add(bSuccess);
}

namespace {

const FString TestSlotName = TEXT("UnitTest_SaveSlot_01");
const FString RecoverySlotName = TEXT("UnitTest_RecoverySlot_02");

/**
 * @brief 测试段状态（保存/加载/迁移断言的载体）
 */
struct FSaveSegmentState {
    int32 Score = 0;
    FString Label;
};

/**
 * @brief 创建独立 GameInstance 并获取其存档子系统
 * @param OutGameInstance 输出由测试负责关闭的 GameInstance
 * @return 独立 GameInstance 上的存档子系统
 */
USaveGameSubsystem* CreateTestSubsystem(UGameInstance*& OutGameInstance) {
    OutGameInstance = NewObject<UGameInstance>(GEngine);
    OutGameInstance->InitializeStandalone(FName(TEXT("SaveGameSubsystemTestWorld")));
    return OutGameInstance->GetSubsystem<USaveGameSubsystem>();
}

/**
 * @brief 注册可执行版本迁移的测试存档段
 * @param Subsystem 目标存档子系统
 * @param State 保存与加载委托共享的测试状态
 * @param Version 当前测试段版本
 */
void RegisterTestSegment(USaveGameSubsystem* Subsystem, FSaveSegmentState& State, int32 Version) {
    FGameSaveSegment Segment;
    Segment.SegmentName = FName(TEXT("UnitTest.Segment"));
    Segment.SegmentVersion = Version;
    Segment.Save = [&State, Version](FArchive& Ar) {
        Ar << State.Score;
        if (Version >= 2) {
            Ar << State.Label;
        }
    };
    Segment.Load = [&State](FArchive& Ar, int32 StoredVersion) {
        Ar << State.Score;
        if (StoredVersion >= 2) {
            Ar << State.Label;
        } else {
            State.Label = TEXT("MigratedFromV1");
        }
        return !Ar.IsError();
    };
    Subsystem->RegisterSegment(Segment);
}

/**
 * @brief 最新异步加载测试的跨续体共享状态
 */
struct FAsyncLoadLatestTestState {
    TStrongObjectPtr<UGameInstance> GameInstance;
    TStrongObjectPtr<UGameSaveTestEventListener> Listener;
    TWeakObjectPtr<USaveGameSubsystem> Subsystem;
    FString SlotName;
    int32 SourceValue = 77;
    int32 LoadedValue = 0;
    int32 ApplyCount = 0;
};

/**
 * @brief 等待最新异步加载测试的全部完成回调并验证结果
 */
class FWaitForAsyncLoadLatestCommand : public IAutomationLatentCommand {
public:
    /**
     * @brief 构造等待命令
     * @param InTest 当前自动化测试上下文
     * @param InState 跨后台任务与主线程续体共享的状态
     */
    FWaitForAsyncLoadLatestCommand(FAutomationTestBase& InTest, const TSharedRef<FAsyncLoadLatestTestState>& InState)
        : Test(InTest)
        , State(InState) {
    }

    /**
     * @brief 在回调未收齐时继续等待，收齐后验证并结束命令
     * @return 两个完成回调均已到达并执行断言返回 true，否则返回 false
     */
    bool Update() override {
        if (State->Listener->CompletionResults.Num() < 2) {
            return false;
        }

        Test.TestEqual(TEXT("只有最新请求应用业务段"), State->ApplyCount, 1);
        Test.TestEqual(TEXT("最新请求应用了存档值"), State->LoadedValue, State->SourceValue);
        Test.TestTrue(TEXT("过期请求以取消结束"), State->Listener->CompletionResults.Contains(false));
        Test.TestTrue(TEXT("最新请求成功完成"), State->Listener->CompletionResults.Contains(true));
        State->Subsystem->DeleteSaveSlot(State->SlotName);
        State->GameInstance->Shutdown();
        return true;
    }

private:
    FAutomationTestBase& Test;
    TSharedRef<FAsyncLoadLatestTestState> State;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveGameSubsystemGenericTest, "Game.Data.Persistence.SegmentSaveAndRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 通用存档测试：白名单、原子写、段版本迁移、容灾恢复与自定义对象逃生口
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FSaveGameSubsystemGenericTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    USaveGameSubsystem* Subsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("临时 GameInstance 上的存档子系统有效"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->SetAllowedSlotNames(TArray<FString>{TestSlotName, RecoverySlotName});
    TestTrue(TEXT("白名单内槽位名允许使用"), Subsystem->IsSlotNameAllowed(TestSlotName));
    TestFalse(TEXT("白名单外槽位名被拒"), Subsystem->IsSlotNameAllowed(TEXT("OtherSlot")));
    TestFalse(TEXT("路径穿越槽位名被拒"), Subsystem->IsSlotNameAllowed(TEXT("../EvilSlot")));
    TestFalse(TEXT("白名单外槽位保存被拒"), Subsystem->SaveGameToSlot(TEXT("OtherSlot")));

    FSaveSegmentState State;
    State.Score = 25;
    State.Label = TEXT("Original");
    RegisterTestSegment(Subsystem, State, 1);
    TestTrue(TEXT("测试段已注册"), Subsystem->HasSegment(FName(TEXT("UnitTest.Segment"))));

    Subsystem->DeleteSaveSlot(TestSlotName);
    TestTrue(TEXT("同步原子保存成功"), Subsystem->SaveGameToSlot(TestSlotName));

    const FString FinalPath = USaveGameSubsystem::GetSaveSlotFilePath(TestSlotName);
    const FString TempPath = USaveGameSubsystem::GetSaveSlotTempFilePath(TestSlotName);
    TestTrue(TEXT("正式存档文件已落盘"), FPaths::FileExists(FinalPath));
    TestFalse(TEXT("写盘后临时文件已清理无残留"), FPaths::FileExists(TempPath));
    TestTrue(TEXT("槽位存在性检测返回 true"), Subsystem->DoesSaveSlotExist(TestSlotName));

    State = FSaveSegmentState();
    TestTrue(TEXT("注销旧版本测试段成功"), Subsystem->UnregisterSegment(FName(TEXT("UnitTest.Segment"))));
    RegisterTestSegment(Subsystem, State, 2);
    TestTrue(TEXT("升级段版本后加载成功（V1 存档迁移到 V2）"), Subsystem->LoadGameFromSlot(TestSlotName));
    TestEqual(TEXT("迁移后旧字段保留"), State.Score, 25);
    TestEqual(TEXT("迁移后新字段使用迁移默认值"), State.Label, FString(TEXT("MigratedFromV1")));

    State.Score = 30;
    State.Label = TEXT("RoundTrip");
    int32 OptionalModuleData = 999;
    FGameSaveSegment OptionalSegment;
    OptionalSegment.SegmentName = FName(TEXT("UnitTest.OptionalModule"));
    OptionalSegment.SegmentVersion = 1;
    OptionalSegment.Save = [&OptionalModuleData](FArchive& Ar) {
        Ar << OptionalModuleData;
    };
    OptionalSegment.Load = [&OptionalModuleData](FArchive& Ar, int32) {
        Ar << OptionalModuleData;
        return !Ar.IsError();
    };
    Subsystem->RegisterSegment(OptionalSegment);
    TestTrue(TEXT("V2 段与可选模块段联合保存成功"), Subsystem->SaveGameToSlot(TestSlotName));

    Subsystem->UnregisterSegment(OptionalSegment.SegmentName);
    State = FSaveSegmentState();
    TestTrue(TEXT("可选模块未注册时加载成功"), Subsystem->LoadGameFromSlot(TestSlotName));
    TestEqual(TEXT("V2 段往返后 Score 一致"), State.Score, 30);
    TestEqual(TEXT("V2 段往返后 Label 一致"), State.Label, FString(TEXT("RoundTrip")));

    TestTrue(TEXT("可选模块未注册期间重存不抹除未知段"), Subsystem->SaveGameToSlot(TestSlotName));
    OptionalModuleData = 0;
    Subsystem->RegisterSegment(OptionalSegment);
    TestTrue(TEXT("可选模块重新注册后加载成功"), Subsystem->LoadGameFromSlot(TestSlotName));
    TestEqual(TEXT("未注册期间保留的可选模块段数据完整恢复"), OptionalModuleData, 999);
    Subsystem->UnregisterSegment(OptionalSegment.SegmentName);

    const FString RecoveryFinalPath = USaveGameSubsystem::GetSaveSlotFilePath(RecoverySlotName);
    const FString RecoveryTempPath = USaveGameSubsystem::GetSaveSlotTempFilePath(RecoverySlotName);
    Subsystem->DeleteSaveSlot(RecoverySlotName);
    TArray<uint8> ValidBytes;
    TestTrue(TEXT("读取基准存档字节成功"), FFileHelper::LoadFileToArray(ValidBytes, *FinalPath));
    TestTrue(TEXT("将字节写入模拟掉电残留的 .tmp 成功"), FFileHelper::SaveArrayToFile(ValidBytes, *RecoveryTempPath));
    TestFalse(TEXT("恢复槽位正式文件尚不存在"), FPaths::FileExists(RecoveryFinalPath));
    TestTrue(TEXT("DoesSaveSlotExist 可检出仅存在 .tmp 的槽位"), Subsystem->DoesSaveSlotExist(RecoverySlotName));
    State = FSaveSegmentState();
    TestTrue(TEXT("遭遇断电残留 .tmp 时能够抢救恢复并成功加载"), Subsystem->LoadGameFromSlot(RecoverySlotName));
    TestEqual(TEXT("恢复后数据完整"), State.Score, 30);
    TestTrue(TEXT("恢复后已原子换入正式文件"), FPaths::FileExists(RecoveryFinalPath));

    TArray<uint8> CorruptedBytes = ValidBytes;
    CorruptedBytes.SetNum(16);
    TestTrue(TEXT("损坏档写盘成功"), FFileHelper::SaveArrayToFile(CorruptedBytes, *FinalPath));
    State = FSaveSegmentState();
    TestFalse(TEXT("损坏档加载被拒"), Subsystem->LoadGameFromSlot(TestSlotName));
    TestEqual(TEXT("加载失败时内存状态不被污染"), State.Score, 0);

    UGameSaveTestArchive* TestArchive = NewObject<UGameSaveTestArchive>(Subsystem);
    TestArchive->Value = 777;
    TestArchive->Label = TEXT("CustomArchive");
    TestTrue(TEXT("自定义 USaveGame 对象保存成功"), Subsystem->SaveObjectToSlot(TestSlotName, TestArchive));
    USaveGame* LoadedObject = nullptr;
    TestTrue(TEXT("自定义 USaveGame 对象加载成功"),
        Subsystem->LoadObjectFromSlot(TestSlotName, UGameSaveTestArchive::StaticClass(), LoadedObject));
    UGameSaveTestArchive* LoadedArchive = Cast<UGameSaveTestArchive>(LoadedObject);
    TestNotNull(TEXT("自定义存档对象类型匹配"), LoadedArchive);
    if (LoadedArchive != nullptr) {
        TestEqual(TEXT("自定义存档字段一致"), LoadedArchive->Value, 777);
        TestEqual(TEXT("自定义存档字符串一致"), LoadedArchive->Label, FString(TEXT("CustomArchive")));
    }

    Subsystem->DeleteSaveSlot(TestSlotName);
    Subsystem->DeleteSaveSlot(RecoverySlotName);
    TestFalse(TEXT("删除后正式文件不存在"), FPaths::FileExists(FinalPath));
    TestFalse(TEXT("删除后恢复文件不存在"), FPaths::FileExists(RecoveryFinalPath));

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameGenerationTest,
    "Game.Data.SaveGeneration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证删除槽位后旧保存代次不能覆盖新档
 * @param Parameters 自动化测试传入参数字符串
 * @return 测试框架入口固定返回 true
 */
bool FSaveGameGenerationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString SlotName = TEXT("UnitTest_SaveGeneration");
    FSaveGameFileIO::DeleteSaveSlot(SlotName);

    UGameSaveTestArchive* OldArchive = NewObject<UGameSaveTestArchive>();
    UGameSaveTestArchive* NewArchive = NewObject<UGameSaveTestArchive>();
    OldArchive->Value = 11;
    NewArchive->Value = 99;

    TArray<uint8> OldBytes;
    TArray<uint8> NewBytes;
    TestTrue(TEXT("旧档内存序列化成功"), FSaveGameFileIO::SerializeObject(OldArchive, OldBytes));
    TestTrue(TEXT("新档内存序列化成功"), FSaveGameFileIO::SerializeObject(NewArchive, NewBytes));

    const uint64 OldGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    FSaveGameFileIO::DeleteSaveSlot(SlotName);
    const uint64 NewGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    TestTrue(TEXT("删除后的新代次严格大于旧代次"), NewGeneration > OldGeneration);

    TestTrue(TEXT("新档写入成功"), FSaveGameFileIO::WriteSlotBytes(SlotName, NewBytes, NewGeneration));
    TestTrue(TEXT("过期写被安全吸收"), FSaveGameFileIO::WriteSlotBytes(SlotName, OldBytes, OldGeneration));

    TArray<uint8> LoadedBytes;
    TestTrue(TEXT("最终存档读取成功"), FSaveGameFileIO::ReadSlotBytes(SlotName, LoadedBytes));
    TestTrue(TEXT("过期写没有覆盖新档"), LoadedBytes == NewBytes);

    FSaveGameFileIO::DeleteSaveSlot(SlotName);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSubsystemAsyncLatestTest,
    "Game.Data.SaveSubsystemAsyncLatest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证同槽位重复异步加载仅应用最新请求并取消过期请求
 * @param Parameters 自动化测试传入参数字符串
 * @return 测试框架入口固定返回 true
 */
bool FSaveGameSubsystemAsyncLatestTest::RunTest(const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    USaveGameSubsystem* Subsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("临时 GameInstance 上的存档子系统有效"), Subsystem);
    if (Subsystem == nullptr) {
        return false;
    }

    const FString SlotName = TEXT("UnitTest_AsyncLatest");
    const FString SegmentName = TEXT("AsyncLatest");
    Subsystem->SetAllowedSlotNames({ SlotName });

    TSharedRef<FAsyncLoadLatestTestState> State = MakeShared<FAsyncLoadLatestTestState>();
    State->GameInstance = TStrongObjectPtr<UGameInstance>(GameInstance);
    State->Listener = TStrongObjectPtr<UGameSaveTestEventListener>(NewObject<UGameSaveTestEventListener>(GameInstance));
    State->Subsystem = Subsystem;
    State->SlotName = SlotName;

    FGameSaveSegment Segment;
    Segment.SegmentName = FName(*SegmentName);
    Segment.SegmentVersion = 1;
    Segment.Save = [State](FArchive& Ar) {
        Ar << State->SourceValue;
    };
    Segment.Load = [State](FArchive& Ar, int32 StoredVersion) {
        if (StoredVersion != 1) {
            return false;
        }
        Ar << State->LoadedValue;
        ++State->ApplyCount;
        return !Ar.IsError();
    };
    Subsystem->RegisterSegment(Segment);
    Subsystem->OnLoadComplete.AddDynamic(State->Listener.Get(), &UGameSaveTestEventListener::RecordLoadComplete);

    TestTrue(TEXT("准备异步加载存档成功"), Subsystem->SaveGameToSlot(SlotName));
    State->LoadedValue = 0;
    State->ApplyCount = 0;

    TestTrue(TEXT("第一个异步加载派发成功"), Subsystem->LoadGameFromSlotAsync(SlotName));
    TestTrue(TEXT("第二个异步加载派发成功"), Subsystem->LoadGameFromSlotAsync(SlotName));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForAsyncLoadLatestCommand(*this, State));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
