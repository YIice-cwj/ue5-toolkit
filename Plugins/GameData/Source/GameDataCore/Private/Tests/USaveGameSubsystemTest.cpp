/**
 * @file USaveGameSubsystemTest.cpp
 * @brief 存档子系统高层调度自动化测试（段版本迁移、蓝图 K2 封装、抽象类拒绝、异步超时防护与容灾恢复）
 */

#include "Save/USaveGameSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Core/DataErrorCodes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Save/FSaveGameFileIO.h"
#include "Tests/GameSaveTestTypes.h"

/**
 * @brief 记录一次异步加载或保存完成结果
 * @param SlotName 完成回调对应的槽位名称（本监听仅记录成功标记）
 * @param bSuccess 操作是否成功
 */
void UGameSaveTestEventListener::RecordLoadComplete([[maybe_unused]] const FString& SlotName, bool bSuccess) {
    CompletionResults.Add(bSuccess);
}

namespace {

const FString TestSlotName = TEXT("UnitTest_SaveSlot_01");
const FString RecoverySlotName = TEXT("UnitTest_RecoverySlot_02");
constexpr double AsyncWaitTimeoutSeconds = 10.0;

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
 * @return 注册是否成功
 */
bool RegisterTestSegment(USaveGameSubsystem* Subsystem, FSaveSegmentState& State, int32 Version) {
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
    return Subsystem->RegisterSegment(Segment).IsSuccess();
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
 * @brief 异步保存测试的跨续体共享状态
 */
struct FAsyncSaveTestState {
    TStrongObjectPtr<UGameInstance> GameInstance;
    TStrongObjectPtr<UGameSaveTestEventListener> Listener;
    TWeakObjectPtr<USaveGameSubsystem> Subsystem;
    FString SlotName;
    int32 ExpectedScore = 888;
    FSaveSegmentState SaveState;
};

/**
 * @brief 等待异步保存完成回调并验证落盘与回读一致性（带超时保护）
 */
class FWaitForAsyncSaveCommand : public IAutomationLatentCommand {
public:
    /**
     * @brief 构造异步保存等待命令
     * @param InTest 当前自动化测试上下文
     * @param InState 跨后台任务与主线程续体共享的状态
     */
    FWaitForAsyncSaveCommand(FAutomationTestBase& InTest, const TSharedRef<FAsyncSaveTestState>& InState)
        : Test(InTest)
        , State(InState)
        , StartTimeSeconds(FPlatformTime::Seconds()) {
    }

    /**
     * @brief 在保存完成回调到达后验证落盘文件与数据回读，超时自动报错并清理环境
     * @return 验证完成或超时返回 true，继续等待返回 false
     */
    bool Update() override {
        if (State->Listener->CompletionResults.Num() < 1) {
            if (FPlatformTime::Seconds() - StartTimeSeconds > AsyncWaitTimeoutSeconds) {
                Test.AddError(TEXT("异步保存等待超时，未收到完成回调"));
                (void)State->Subsystem->DeleteSaveSlot(State->SlotName);
                State->GameInstance->Shutdown();
                return true;
            }
            return false;
        }

        Test.TestTrue(TEXT("异步保存完成且结果为成功"), State->Listener->CompletionResults[0]);
        Test.TestTrue(TEXT("异步保存正式文件已落盘"), FPaths::FileExists(FSaveGameFileIO::GetSaveSlotFilePath(State->SlotName)));

        FSaveSegmentState ReadState;
        Test.TestTrue(TEXT("重新注册读回段成功"), RegisterTestSegment(State->Subsystem.Get(), ReadState, 1));
        Test.TestTrue(TEXT("异步保存后数据可成功同步回读"), State->Subsystem->LoadGameFromSlot(State->SlotName).IsSuccess());
        Test.TestEqual(TEXT("异步保存回读数值与预期一致"), ReadState.Score, State->ExpectedScore);

        (void)State->Subsystem->DeleteSaveSlot(State->SlotName);
        State->GameInstance->Shutdown();
        return true;
    }

private:
    FAutomationTestBase& Test;
    TSharedRef<FAsyncSaveTestState> State;
    double StartTimeSeconds = 0.0;
};

/**
 * @brief 等待最新异步加载测试的全部完成回调并验证结果（带超时保护）
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
        , State(InState)
        , StartTimeSeconds(FPlatformTime::Seconds()) {
    }

    /**
     * @brief 在回调未收齐时继续等待，收齐或超时后验证并结束命令
     * @return 两个完成回调均已到达或超时返回 true，否则返回 false
     */
    bool Update() override {
        if (State->Listener->CompletionResults.Num() < 2) {
            if (FPlatformTime::Seconds() - StartTimeSeconds > AsyncWaitTimeoutSeconds) {
                Test.AddError(TEXT("异步最新加载等待超时，未收齐两个完成回调"));
                (void)State->Subsystem->DeleteSaveSlot(State->SlotName);
                State->GameInstance->Shutdown();
                return true;
            }
            return false;
        }

        Test.TestEqual(TEXT("只有最新请求应用业务段"), State->ApplyCount, 1);
        Test.TestEqual(TEXT("最新请求应用了存档值"), State->LoadedValue, State->SourceValue);
        Test.TestTrue(TEXT("过期请求以取消结束"), State->Listener->CompletionResults.Contains(false));
        Test.TestTrue(TEXT("最新请求成功完成"), State->Listener->CompletionResults.Contains(true));
        (void)State->Subsystem->DeleteSaveSlot(State->SlotName);
        State->GameInstance->Shutdown();
        return true;
    }

private:
    FAutomationTestBase& Test;
    TSharedRef<FAsyncLoadLatestTestState> State;
    double StartTimeSeconds = 0.0;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSubsystemGenericTest,
    "Game.Data.Persistence.SegmentSaveAndRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 通用存档测试：白名单、原子写、段版本迁移、K2 蓝图接口、抽象类拒绝、容灾恢复与逃生口
 * @param Parameters 框架参数
 * @return 全部断言通过返回 true
 */
bool FSaveGameSubsystemGenericTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    USaveGameSubsystem* Subsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("临时 GameInstance 上的存档子系统有效"), Subsystem);
    if (!Subsystem) {
        if (GameInstance) {
            GameInstance->Shutdown();
        }
        return false;
    }

    TestEqual(TEXT("USaveGameSubsystem::GetPtr 正确解析"), USaveGameSubsystem::GetPtr(GameInstance), Subsystem);
    TestEqual(TEXT("USaveGameSubsystem::Get 正确解析"), &USaveGameSubsystem::Get(GameInstance), Subsystem);

    Subsystem->SetAllowedSlotNames(TArray<FString>{TestSlotName, RecoverySlotName});
    TestTrue(TEXT("白名单内槽位名允许使用"), Subsystem->IsSlotNameAllowed(TestSlotName));
    TestFalse(TEXT("白名单外槽位名被拒"), Subsystem->IsSlotNameAllowed(TEXT("OtherSlot")));
    TestFalse(TEXT("路径穿越槽位名被拒"), Subsystem->IsSlotNameAllowed(TEXT("../EvilSlot")));
    TestFalse(TEXT("反斜杠路径穿越槽位名被拒"), Subsystem->IsSlotNameAllowed(TEXT("Evil\\Slot")));
    TestFalse(TEXT("空槽位名被拒"), Subsystem->IsSlotNameAllowed(TEXT("")));
    TestFalse(TEXT("超长槽位名被拒"), Subsystem->IsSlotNameAllowed(FString::ChrN(65, 'X')));
    TestTrue(TEXT("白名单外槽位保存被拒"), Subsystem->SaveGameToSlot(TEXT("OtherSlot")).IsFailure());

    FSaveSegmentState State;
    State.Score = 25;
    State.Label = TEXT("Original");
    TestTrue(TEXT("注册 V1 测试段成功"), RegisterTestSegment(Subsystem, State, 1));
    TestTrue(TEXT("测试段已注册"), Subsystem->HasSegment(FName(TEXT("UnitTest.Segment"))));

    (void)Subsystem->DeleteSaveSlot(TestSlotName);
    TestTrue(TEXT("同步原子保存成功"), Subsystem->SaveGameToSlot(TestSlotName).IsSuccess());

    const FString FinalPath = FSaveGameFileIO::GetSaveSlotFilePath(TestSlotName);
    const FString TempPath = FSaveGameFileIO::GetSaveSlotTempFilePath(TestSlotName);
    TestTrue(TEXT("正式存档文件已落盘"), FPaths::FileExists(FinalPath));
    TestFalse(TEXT("写盘后临时文件已清理无残留"), FPaths::FileExists(TempPath));
    TestTrue(TEXT("槽位存在性检测返回 true"), Subsystem->DoesSaveSlotExist(TestSlotName));

    State = FSaveSegmentState();
    TestTrue(TEXT("蓝图 K2_LoadGameFromSlot 成功"), Subsystem->K2_LoadGameFromSlot(TestSlotName));
    TestEqual(TEXT("K2_LoadGameFromSlot 恢复数值一致"), State.Score, 25);
    State.Score = 26;
    TestTrue(TEXT("蓝图 K2_SaveGameToSlot 成功"), Subsystem->K2_SaveGameToSlot(TestSlotName));

    State = FSaveSegmentState();
    TestTrue(TEXT("注销旧版本测试段成功"), Subsystem->UnregisterSegment(FName(TEXT("UnitTest.Segment"))).IsSuccess());
    TestTrue(TEXT("注册 V2 测试段成功"), RegisterTestSegment(Subsystem, State, 2));
    TestTrue(TEXT("升级段版本后加载成功（V1 存档迁移到 V2）"), Subsystem->LoadGameFromSlot(TestSlotName).IsSuccess());
    TestEqual(TEXT("迁移后旧字段保留"), State.Score, 26);
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
    TestTrue(TEXT("注册可选模块段成功"), Subsystem->RegisterSegment(OptionalSegment).IsSuccess());
    TestTrue(TEXT("V2 段与可选模块段联合保存成功"), Subsystem->SaveGameToSlot(TestSlotName).IsSuccess());

    TestTrue(TEXT("注销可选模块段成功"), Subsystem->UnregisterSegment(OptionalSegment.SegmentName).IsSuccess());
    State = FSaveSegmentState();
    TestTrue(TEXT("可选模块未注册时加载成功"), Subsystem->LoadGameFromSlot(TestSlotName).IsSuccess());
    TestEqual(TEXT("V2 段往返后 Score 一致"), State.Score, 30);
    TestEqual(TEXT("V2 段往返后 Label 一致"), State.Label, FString(TEXT("RoundTrip")));

    TestTrue(TEXT("可选模块未注册期间重存不抹除未知段"), Subsystem->SaveGameToSlot(TestSlotName).IsSuccess());
    OptionalModuleData = 0;
    TestTrue(TEXT("重新注册可选模块段成功"), Subsystem->RegisterSegment(OptionalSegment).IsSuccess());
    TestTrue(TEXT("可选模块重新注册后加载成功"), Subsystem->LoadGameFromSlot(TestSlotName).IsSuccess());
    TestEqual(TEXT("未注册期间保留的可选模块段数据完整恢复"), OptionalModuleData, 999);
    TestTrue(TEXT("再次注销可选模块段成功"), Subsystem->UnregisterSegment(OptionalSegment.SegmentName).IsSuccess());

    const FString RecoveryFinalPath = FSaveGameFileIO::GetSaveSlotFilePath(RecoverySlotName);
    const FString RecoveryTempPath = FSaveGameFileIO::GetSaveSlotTempFilePath(RecoverySlotName);
    (void)Subsystem->DeleteSaveSlot(RecoverySlotName);
    TArray<uint8> ValidBytes;
    TestTrue(TEXT("读取基准存档字节成功"), FFileHelper::LoadFileToArray(ValidBytes, *FinalPath));
    TestTrue(TEXT("将字节写入模拟掉电残留的 .tmp 成功"), FFileHelper::SaveArrayToFile(ValidBytes, *RecoveryTempPath));
    TestFalse(TEXT("恢复槽位正式文件尚不存在"), FPaths::FileExists(RecoveryFinalPath));
    TestTrue(TEXT("DoesSaveSlotExist 可检出仅存在 .tmp 的槽位"), Subsystem->DoesSaveSlotExist(RecoverySlotName));
    State = FSaveSegmentState();
    TestTrue(TEXT("遭遇断电残留 .tmp 时能够抢救恢复并成功加载"), Subsystem->LoadGameFromSlot(RecoverySlotName).IsSuccess());
    TestEqual(TEXT("恢复后数据完整"), State.Score, 30);
    TestTrue(TEXT("恢复后已原子换入正式文件"), FPaths::FileExists(RecoveryFinalPath));

    TArray<uint8> CorruptedBytes = ValidBytes;
    CorruptedBytes.SetNum(16);
    TestTrue(TEXT("损坏档写盘成功"), FFileHelper::SaveArrayToFile(CorruptedBytes, *FinalPath));
    State = FSaveSegmentState();
    TestTrue(TEXT("损坏档加载被拒"), Subsystem->LoadGameFromSlot(TestSlotName).IsFailure());
    TestEqual(TEXT("加载失败时内存状态不被污染"), State.Score, 0);

    TArray<uint8> MagicCorrupted64Bytes;
    MagicCorrupted64Bytes.Init(0, 64);
    TestTrue(TEXT("Magic 损坏档写盘成功"), FFileHelper::SaveArrayToFile(MagicCorrupted64Bytes, *FinalPath));
    TestTrue(TEXT("Magic 损坏档加载被拒"), Subsystem->LoadGameFromSlot(TestSlotName).IsFailure());

    TArray<uint8> CorruptedTmp10Bytes;
    CorruptedTmp10Bytes.Init(0, 10);
    TestTrue(TEXT("损坏 tmp 写盘成功"), FFileHelper::SaveArrayToFile(CorruptedTmp10Bytes, *TempPath));
    TestTrue(TEXT("主副文件均损坏时加载被拒"), Subsystem->LoadGameFromSlot(TestSlotName).IsFailure());
    (void)Subsystem->DeleteSaveSlot(TestSlotName);

    FSaveSegmentState HigherState;
    TestTrue(TEXT("注册 V3 测试段成功"), RegisterTestSegment(Subsystem, HigherState, 3));
    TestTrue(TEXT("以 V3 段保存成功"), Subsystem->SaveGameToSlot(TestSlotName).IsSuccess());
    TestTrue(TEXT("注销 V3 测试段成功"), Subsystem->UnregisterSegment(FName(TEXT("UnitTest.Segment"))).IsSuccess());
    TestTrue(TEXT("注册 V1 测试段成功"), RegisterTestSegment(Subsystem, HigherState, 1));
    AddExpectedErrorPlain(TEXT("高于当前段版本"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto HighVerLoadRes = Subsystem->LoadGameFromSlot(TestSlotName);
    if (TestTrue(TEXT("落盘段版本高于当前代码支持版本时拒绝加载"), HighVerLoadRes.IsFailure())) {
        TestEqual(TEXT("高版本段加载错误码为 SegmentVersionMismatch"),
            HighVerLoadRes.GetError().ErrorCode, Game::DataErrorCodes::SegmentVersionMismatch);
    }
    TestTrue(TEXT("注销 V1 测试段成功"), Subsystem->UnregisterSegment(FName(TEXT("UnitTest.Segment"))).IsSuccess());
    (void)Subsystem->DeleteSaveSlot(TestSlotName);

    Subsystem->SetSaveGameClass(nullptr);
    TestEqual(TEXT("设置空类保持默认 UGameSaveArchive"), Subsystem->GetSaveGameClass(), TSubclassOf<UGameSaveArchive>(UGameSaveArchive::StaticClass()));
    Subsystem->SetSaveGameClass(UGameSaveAbstractArchive::StaticClass());
    TestEqual(TEXT("设置抽象类应被拒绝并保持默认 UGameSaveArchive"), Subsystem->GetSaveGameClass(), TSubclassOf<UGameSaveArchive>(UGameSaveArchive::StaticClass()));
    Subsystem->SetSaveGameClass(UGameSaveArchive::StaticClass());
    TestEqual(TEXT("获取默认类正确"), Subsystem->GetSaveGameClass(), TSubclassOf<UGameSaveArchive>(UGameSaveArchive::StaticClass()));

    UGameSaveTestArchive* TestArchive = NewObject<UGameSaveTestArchive>(Subsystem);
    TestArchive->Value = 777;
    TestArchive->Label = TEXT("CustomArchive");

    const auto DisallowedSaveObjRes = Subsystem->SaveObjectToSlot(TEXT("Disallowed"), TestArchive);
    if (TestTrue(TEXT("白名单外 SaveObjectToSlot 被拒"), DisallowedSaveObjRes.IsFailure())) {
        TestEqual(TEXT("白名单外错误码为 InvalidSlotName"), DisallowedSaveObjRes.GetError().ErrorCode, Game::DataErrorCodes::InvalidSlotName);
    }

    const auto NullObjSaveRes = Subsystem->SaveObjectToSlot(TestSlotName, nullptr);
    if (TestTrue(TEXT("nullptr 对象 SaveObjectToSlot 被拒"), NullObjSaveRes.IsFailure())) {
        TestEqual(TEXT("nullptr 对象错误码为 InvalidSaveObject"), NullObjSaveRes.GetError().ErrorCode, Game::DataErrorCodes::InvalidSaveObject);
    }

    const auto DisallowedLoadObjRes = Subsystem->LoadObjectFromSlot(TEXT("Disallowed"), UGameSaveTestArchive::StaticClass());
    TestTrue(TEXT("白名单外 LoadObjectFromSlot 被拒"), DisallowedLoadObjRes.IsFailure());

    const auto NullClassLoadRes = Subsystem->LoadObjectFromSlot(TestSlotName, nullptr);
    if (TestTrue(TEXT("nullptr 类 LoadObjectFromSlot 被拒"), NullClassLoadRes.IsFailure())) {
        TestEqual(TEXT("nullptr 类错误码为 InvalidSaveClass"), NullClassLoadRes.GetError().ErrorCode, Game::DataErrorCodes::InvalidSaveClass);
    }

    const auto AbstractClassLoadRes = Subsystem->LoadObjectFromSlot(TestSlotName, UGameSaveAbstractArchive::StaticClass());
    if (TestTrue(TEXT("抽象类 LoadObjectFromSlot 被拒"), AbstractClassLoadRes.IsFailure())) {
        TestEqual(TEXT("抽象类错误码为 InvalidSaveClass"), AbstractClassLoadRes.GetError().ErrorCode, Game::DataErrorCodes::InvalidSaveClass);
    }

    TestFalse(TEXT("白名单外 DoesSaveSlotExist 被拒"), Subsystem->DoesSaveSlotExist(TEXT("Disallowed")));
    TestTrue(TEXT("白名单外 DeleteSaveSlot 被拒"), Subsystem->DeleteSaveSlot(TEXT("Disallowed")).IsFailure());
    TestTrue(TEXT("白名单外 LoadGameFromSlot 被拒"), Subsystem->LoadGameFromSlot(TEXT("Disallowed")).IsFailure());

    TestTrue(TEXT("蓝图 K2_SaveObjectToSlot 成功"), Subsystem->K2_SaveObjectToSlot(TestSlotName, TestArchive));
    USaveGame* BlueprintLoadedObj = nullptr;
    TestTrue(TEXT("蓝图 K2_LoadObjectFromSlot 成功"),
        Subsystem->K2_LoadObjectFromSlot(TestSlotName, UGameSaveTestArchive::StaticClass(), BlueprintLoadedObj));
    TestNotNull(TEXT("蓝图 K2_LoadObjectFromSlot 输出对象有效"), BlueprintLoadedObj);

    const auto TryLoadObjRes = Subsystem->LoadObjectFromSlot<UGameSaveTestArchive>(TestSlotName);
    TestTrue(TEXT("LoadObjectFromSlot 模板成功"), TryLoadObjRes.IsSuccess());
    if (TryLoadObjRes.IsSuccess()) {
        UGameSaveTestArchive* TypedArchive = TryLoadObjRes.GetValue();
        if (TestNotNull(TEXT("LoadObjectFromSlot 模板返回非空"), TypedArchive)) {
            TestEqual(TEXT("LoadObjectFromSlot 模板数值匹配"), TypedArchive->Value, 777);
            TestEqual(TEXT("LoadObjectFromSlot 模板字符串匹配"), TypedArchive->Label, FString(TEXT("CustomArchive")));
        }
    }

    AddExpectedErrorPlain(TEXT("类型不匹配"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto WrongTypeRes = Subsystem->LoadObjectFromSlot<UGameSaveArchive>(TestSlotName);
    if (TestTrue(TEXT("类型不匹配时模板 LoadObjectFromSlot 被拒"), WrongTypeRes.IsFailure())) {
        TestEqual(TEXT("类型不匹配错误码为 SaveTypeMismatch"), WrongTypeRes.GetError().ErrorCode, Game::DataErrorCodes::SaveTypeMismatch);
    }

    UGameSaveArchive* FutureArchive = NewObject<UGameSaveArchive>(Subsystem);
    FutureArchive->Metadata.ArchiveVersion = UGameSaveArchive::CurrentArchiveVersion + 1;
    TestTrue(TEXT("未来版本归档保存成功"), Subsystem->SaveObjectToSlot(TestSlotName, FutureArchive).IsSuccess());
    AddExpectedErrorPlain(TEXT("高于当前支持版本"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto FutureArchiveLoadRes = Subsystem->LoadGameFromSlot(TestSlotName);
    if (TestTrue(TEXT("未来版本归档加载被拒"), FutureArchiveLoadRes.IsFailure())) {
        TestEqual(TEXT("未来归档错误码为 ArchiveVersionMismatch"),
            FutureArchiveLoadRes.GetError().ErrorCode, Game::DataErrorCodes::ArchiveVersionMismatch);
    }

    UGameSaveArchive* ZeroVersionArchive = NewObject<UGameSaveArchive>(Subsystem);
    ZeroVersionArchive->Metadata.ArchiveVersion = 0;
    TestTrue(TEXT("零版本归档保存成功"), Subsystem->SaveObjectToSlot(TestSlotName, ZeroVersionArchive).IsSuccess());
    AddExpectedErrorPlain(TEXT("非法（须为正整数）"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto ZeroArchiveLoadRes = Subsystem->LoadGameFromSlot(TestSlotName);
    if (TestTrue(TEXT("零或负版本归档加载被拒"), ZeroArchiveLoadRes.IsFailure())) {
        TestEqual(TEXT("零版本归档错误码为 ArchiveVersionMismatch"),
            ZeroArchiveLoadRes.GetError().ErrorCode, Game::DataErrorCodes::ArchiveVersionMismatch);
    }

    TestTrue(TEXT("蓝图 K2_DeleteSaveSlot 删除成功"), Subsystem->K2_DeleteSaveSlot(TestSlotName));
    TestTrue(TEXT("清理恢复槽位成功"), Subsystem->DeleteSaveSlot(RecoverySlotName).IsSuccess());
    TestFalse(TEXT("删除后正式文件不存在"), FPaths::FileExists(FinalPath));
    TestFalse(TEXT("删除后恢复文件不存在"), FPaths::FileExists(RecoveryFinalPath));

    Subsystem->Deinitialize();
    TestFalse(TEXT("Deinitialize 后段已清空"), Subsystem->HasSegment(FName(TEXT("UnitTest.Segment"))));

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSubsystemAsyncSaveTest,
    "Game.Data.SaveSubsystemAsyncSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证主线程发起异步保存、后台写盘、完成委托广播与数据一致性
 * @param Parameters 自动化测试参数
 * @return 测试框架入口固定返回 true
 */
bool FSaveGameSubsystemAsyncSaveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    USaveGameSubsystem* Subsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("临时 GameInstance 上的存档子系统有效"), Subsystem);
    if (Subsystem == nullptr) {
        if (GameInstance) {
            GameInstance->Shutdown();
        }
        return false;
    }

    const FString SlotName = TEXT("UnitTest_AsyncSave");
    Subsystem->SetAllowedSlotNames({ SlotName });

    TSharedRef<FAsyncSaveTestState> State = MakeShared<FAsyncSaveTestState>();
    State->GameInstance = TStrongObjectPtr<UGameInstance>(GameInstance);
    State->Listener = TStrongObjectPtr<UGameSaveTestEventListener>(NewObject<UGameSaveTestEventListener>(GameInstance));
    State->Subsystem = Subsystem;
    State->SlotName = SlotName;
    State->ExpectedScore = 888;
    State->SaveState.Score = State->ExpectedScore;
    TestTrue(TEXT("注册异步保存段成功"), RegisterTestSegment(Subsystem, State->SaveState, 1));

    Subsystem->OnSaveComplete.AddDynamic(State->Listener.Get(), &UGameSaveTestEventListener::RecordLoadComplete);

    TestFalse(TEXT("白名单外异步保存立即被拒"), Subsystem->SaveGameToSlotAsync(TEXT("DisallowedSlot")));
    if (TestEqual(TEXT("白名单外异步保存立即广播失败"), State->Listener->CompletionResults.Num(), 1)) {
        TestFalse(TEXT("白名单外广播结果为 false"), State->Listener->CompletionResults[0]);
    }
    State->Listener->CompletionResults.Reset();

    (void)Subsystem->DeleteSaveSlot(SlotName);
    TestTrue(TEXT("合法槽位异步保存派发成功"), Subsystem->SaveGameToSlotAsync(SlotName));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForAsyncSaveCommand(*this, State));
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
bool FSaveGameSubsystemAsyncLatestTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    USaveGameSubsystem* Subsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("临时 GameInstance 上的存档子系统有效"), Subsystem);
    if (Subsystem == nullptr) {
        if (GameInstance) {
            GameInstance->Shutdown();
        }
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
    TestTrue(TEXT("注册异步加载测试段成功"), Subsystem->RegisterSegment(Segment).IsSuccess());
    Subsystem->OnLoadComplete.AddDynamic(State->Listener.Get(), &UGameSaveTestEventListener::RecordLoadComplete);

    TestTrue(TEXT("准备异步加载存档成功"), Subsystem->SaveGameToSlot(SlotName).IsSuccess());
    State->LoadedValue = 0;
    State->ApplyCount = 0;

    TestFalse(TEXT("白名单外异步装载立即被拒"), Subsystem->LoadGameFromSlotAsync(TEXT("DisallowedSlot")));
    if (TestEqual(TEXT("白名单外异步装载立即广播失败"), State->Listener->CompletionResults.Num(), 1)) {
        TestFalse(TEXT("白名单外广播结果为 false"), State->Listener->CompletionResults[0]);
    }
    State->Listener->CompletionResults.Reset();

    TestTrue(TEXT("第一个异步加载派发成功"), Subsystem->LoadGameFromSlotAsync(SlotName));
    TestTrue(TEXT("第二个异步加载派发成功"), Subsystem->LoadGameFromSlotAsync(SlotName));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForAsyncLoadLatestCommand(*this, State));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
