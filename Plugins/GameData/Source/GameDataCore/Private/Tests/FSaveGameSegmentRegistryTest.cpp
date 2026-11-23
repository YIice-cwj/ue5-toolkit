/**
 * @file FSaveGameSegmentRegistryTest.cpp
 * @brief 存档分段注册表自动化测试（注册契约、版本预检、序列化失败、未知段保留与复位）
 */

#include "Save/FSaveGameSegmentRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Core/DataErrorCodes.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSegmentRegistryTest,
    "Game.Data.Save.SegmentRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证分段注册表契约校验、只读版本预检、归档构建/应用、失败路径、未知段保留与复位
 * @param Parameters 自动化测试参数
 * @return 测试框架入口固定返回 true
 */
bool FSaveGameSegmentRegistryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FSaveGameSegmentRegistry Registry;

    AddExpectedErrorPlain(TEXT("存档段注册被拒"), EAutomationExpectedErrorFlags::Contains, 4);

    FGameSaveSegment InvalidNone;
    InvalidNone.SegmentName = NAME_None;
    InvalidNone.Save = [](FArchive&) {};
    InvalidNone.Load = [](FArchive&, int32) { return true; };
    const auto NoneRes = Registry.RegisterSegment(InvalidNone);
    if (TestTrue(TEXT("空段名注册返回 Failure"), NoneRes.IsFailure())) {
        TestEqual(TEXT("空段名错误码为 InvalidSegment"), NoneRes.GetError().ErrorCode, Game::DataErrorCodes::InvalidSegment);
    }
    TestFalse(TEXT("空段名未登记"), Registry.HasSegment(NAME_None));

    FGameSaveSegment InvalidNoSave;
    InvalidNoSave.SegmentName = FName(TEXT("NoSave"));
    InvalidNoSave.Load = [](FArchive&, int32) { return true; };
    TestTrue(TEXT("空保存委托段注册返回 Failure"), Registry.RegisterSegment(InvalidNoSave).IsFailure());
    TestFalse(TEXT("空保存委托段未登记"), Registry.HasSegment(FName(TEXT("NoSave"))));

    FGameSaveSegment InvalidNoLoad;
    InvalidNoLoad.SegmentName = FName(TEXT("NoLoad"));
    InvalidNoLoad.Save = [](FArchive&) {};
    TestTrue(TEXT("空加载委托段注册返回 Failure"), Registry.RegisterSegment(InvalidNoLoad).IsFailure());
    TestFalse(TEXT("空加载委托段未登记"), Registry.HasSegment(FName(TEXT("NoLoad"))));

    FGameSaveSegment InvalidZeroVersion;
    InvalidZeroVersion.SegmentName = FName(TEXT("ZeroVer"));
    InvalidZeroVersion.SegmentVersion = 0;
    InvalidZeroVersion.Save = [](FArchive&) {};
    InvalidZeroVersion.Load = [](FArchive&, int32) { return true; };
    const auto ZeroVerRes = Registry.RegisterSegment(InvalidZeroVersion);
    if (TestTrue(TEXT("版本号 <= 0 注册返回 Failure"), ZeroVerRes.IsFailure())) {
        TestEqual(TEXT("非法版本号错误码为 InvalidSegment"), ZeroVerRes.GetError().ErrorCode, Game::DataErrorCodes::InvalidSegment);
    }

    const auto MissingUnreg = Registry.UnregisterSegment(FName(TEXT("Missing")));
    if (TestTrue(TEXT("未注册段注销返回 Failure"), MissingUnreg.IsFailure())) {
        TestEqual(TEXT("未注册段注销错误码为 SegmentNotFound"), MissingUnreg.GetError().ErrorCode, Game::DataErrorCodes::SegmentNotFound);
    }
    TestFalse(TEXT("未注册段查询返回 false"), Registry.HasSegment(FName(TEXT("Missing"))));

    int32 SourceValue = 42;
    int32 LoadedValue = 0;
    const FName SegmentName(TEXT("ModuleA"));

    FGameSaveSegment ValidSegment;
    ValidSegment.SegmentName = SegmentName;
    ValidSegment.SegmentVersion = 1;
    ValidSegment.Save = [&SourceValue](FArchive& Ar) {
        Ar << SourceValue;
    };
    ValidSegment.Load = [&LoadedValue](FArchive& Ar, int32) {
        Ar << LoadedValue;
        return !Ar.IsError();
    };
    TestTrue(TEXT("合法段注册返回 Success"), Registry.RegisterSegment(ValidSegment).IsSuccess());
    TestTrue(TEXT("合法段查询返回 true"), Registry.HasSegment(SegmentName));

    const FString SlotName = TEXT("Slot_RegistryTest");
    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>();
    TestTrue(TEXT("归档构建成功"), Registry.BuildArchive(SlotName, *Archive).IsSuccess());
    TestTrue(TEXT("归档包含目标段"), Archive->Segments.Contains(SegmentName));
    TestTrue(TEXT("只读版本预检通过"), Registry.ValidateArchiveCompatibility(*Archive).IsSuccess());
    TestEqual(TEXT("只读版本预检不修改业务内存状态"), LoadedValue, 0);

    TestTrue(TEXT("归档应用成功"), Registry.ApplyArchive(SlotName, *Archive).IsSuccess());
    TestEqual(TEXT("反序列化值一致"), LoadedValue, 42);

    UGameSaveArchive* FutureArchive = NewObject<UGameSaveArchive>();
    FGameSaveSegmentPayload FuturePayload;
    FuturePayload.SegmentVersion = 99;
    FMemoryWriter Writer(FuturePayload.Bytes);
    int32 Dummy = 100;
    Writer << Dummy;
    FutureArchive->Segments.Add(SegmentName, MoveTemp(FuturePayload));

    AddExpectedErrorPlain(TEXT("高于当前段版本"), EAutomationExpectedErrorFlags::Contains, 2);
    const auto PreCheckFail = Registry.ValidateArchiveCompatibility(*FutureArchive);
    if (TestTrue(TEXT("只读预检拒绝高版本段"), PreCheckFail.IsFailure())) {
        TestEqual(TEXT("只读预检错误码为 SegmentVersionMismatch"), PreCheckFail.GetError().ErrorCode, Game::DataErrorCodes::SegmentVersionMismatch);
    }

    const auto ApplyFutureFail = Registry.ApplyArchive(SlotName, *FutureArchive);
    if (TestTrue(TEXT("高版本段拒绝应用"), ApplyFutureFail.IsFailure())) {
        TestEqual(TEXT("高版本段应用错误码为 SegmentVersionMismatch"), ApplyFutureFail.GetError().ErrorCode, Game::DataErrorCodes::SegmentVersionMismatch);
    }

    UGameSaveArchive* ZeroSegVerArchive = NewObject<UGameSaveArchive>();
    FGameSaveSegmentPayload ZeroVerPayload;
    ZeroVerPayload.SegmentVersion = 0;
    ZeroSegVerArchive->Segments.Add(SegmentName, MoveTemp(ZeroVerPayload));
    AddExpectedErrorPlain(TEXT("非法（须为正整数）"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto ZeroVerFail = Registry.ValidateArchiveCompatibility(*ZeroSegVerArchive);
    if (TestTrue(TEXT("只读预检拒绝版本号 <= 0 的非法段载荷"), ZeroVerFail.IsFailure())) {
        TestEqual(TEXT("零版本段预检错误码为 SegmentVersionMismatch"), ZeroVerFail.GetError().ErrorCode, Game::DataErrorCodes::SegmentVersionMismatch);
    }

    FGameSaveSegment StringBombSegment;
    StringBombSegment.SegmentName = FName(TEXT("StringBomb"));
    StringBombSegment.SegmentVersion = 1;
    StringBombSegment.Save = [](FArchive&) {};
    StringBombSegment.Load = [](FArchive& Ar, int32) {
        FString LoadedText;
        Ar << LoadedText;
        return !Ar.IsError();
    };
    TestTrue(TEXT("注册字符串炸弹防护测试段成功"), Registry.RegisterSegment(StringBombSegment).IsSuccess());
    FGameSaveSegmentPayload BombPayload;
    BombPayload.SegmentVersion = 1;
    FMemoryWriter BombWriter(BombPayload.Bytes);
    int32 HugeSaveNum = 0x7FFFFFFF;
    BombWriter << HugeSaveNum;
    UGameSaveArchive* BombArchive = NewObject<UGameSaveArchive>();
    BombArchive->Segments.Add(StringBombSegment.SegmentName, MoveTemp(BombPayload));
    AddExpectedErrorPlain(TEXT("String is too large"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("存档段加载失败"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto BombRes = Registry.ApplyArchive(SlotName, *BombArchive);
    if (TestTrue(TEXT("伪造 2GB FString 段负载应被 ArMaxSerializeSize 安全拦截"), BombRes.IsFailure())) {
        TestEqual(TEXT("字符串炸弹拦截返回 SegmentLoadFailed"), BombRes.GetError().ErrorCode, Game::DataErrorCodes::SegmentLoadFailed);
    }
    TestTrue(TEXT("注销字符串炸弹测试段成功"), Registry.UnregisterSegment(StringBombSegment.SegmentName).IsSuccess());

    FGameSaveSegment FailingSaveSeg;
    FailingSaveSeg.SegmentName = FName(TEXT("FailingSave"));
    FailingSaveSeg.SegmentVersion = 1;
    FailingSaveSeg.Save = [](FArchive& Ar) {
        Ar.SetError();
    };
    FailingSaveSeg.Load = [](FArchive&, int32) {
        return false;
    };
    TestTrue(TEXT("注册失败测试段成功"), Registry.RegisterSegment(FailingSaveSeg).IsSuccess());
    UGameSaveArchive* FailSaveArchive = NewObject<UGameSaveArchive>();
    AddExpectedErrorPlain(TEXT("存档段保存失败"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto BuildFailRes = Registry.BuildArchive(SlotName, *FailSaveArchive);
    if (TestTrue(TEXT("归档构建遭遇段序列化错误返回 Failure"), BuildFailRes.IsFailure())) {
        TestEqual(TEXT("构建失败错误码为 SegmentSaveFailed"), BuildFailRes.GetError().ErrorCode, Game::DataErrorCodes::SegmentSaveFailed);
    }

    FGameSaveSegmentPayload FailingLoadPayload;
    FailingLoadPayload.SegmentVersion = 1;
    UGameSaveArchive* FailLoadArchive = NewObject<UGameSaveArchive>();
    FailLoadArchive->Segments.Add(FailingSaveSeg.SegmentName, MoveTemp(FailingLoadPayload));
    AddExpectedErrorPlain(TEXT("存档段加载失败"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto LoadFailRes = Registry.ApplyArchive(SlotName, *FailLoadArchive);
    if (TestTrue(TEXT("段 Load 返回 false 时返回 Failure"), LoadFailRes.IsFailure())) {
        TestEqual(TEXT("段加载失败错误码为 SegmentLoadFailed"), LoadFailRes.GetError().ErrorCode, Game::DataErrorCodes::SegmentLoadFailed);
    }
    TestTrue(TEXT("注销失败测试段成功"), Registry.UnregisterSegment(FailingSaveSeg.SegmentName).IsSuccess());

    UGameSaveArchive* UnknownArchive = NewObject<UGameSaveArchive>();
    const FName UnknownSegmentName(TEXT("UnknownModule"));
    FGameSaveSegmentPayload UnknownPayload;
    UnknownPayload.SegmentVersion = 1;
    FMemoryWriter UnknownWriter(UnknownPayload.Bytes);
    int32 UnknownData = 999;
    UnknownWriter << UnknownData;
    UnknownArchive->Segments.Add(UnknownSegmentName, MoveTemp(UnknownPayload));

    TestTrue(TEXT("包含未知段归档应用成功"), Registry.ApplyArchive(SlotName, *UnknownArchive).IsSuccess());

    UGameSaveArchive* RebuiltArchive = NewObject<UGameSaveArchive>();
    TestTrue(TEXT("重新构建归档成功"), Registry.BuildArchive(SlotName, *RebuiltArchive).IsSuccess());
    TestTrue(TEXT("重新构建归档保留了未知段"), RebuiltArchive->Segments.Contains(UnknownSegmentName));

    Registry.ClearPreservedSegmentsForSlot(SlotName);
    UGameSaveArchive* ClearedArchive = NewObject<UGameSaveArchive>();
    TestTrue(TEXT("清理保留段后重新构建归档成功"), Registry.BuildArchive(SlotName, *ClearedArchive).IsSuccess());
    TestFalse(TEXT("清理保留段后未知段不再输出"), ClearedArchive->Segments.Contains(UnknownSegmentName));

    TestTrue(TEXT("注销段成功"), Registry.UnregisterSegment(SegmentName).IsSuccess());
    TestFalse(TEXT("注销后段不存在"), Registry.HasSegment(SegmentName));

    TestTrue(TEXT("再次注册合法段成功"), Registry.RegisterSegment(ValidSegment).IsSuccess());
    Registry.Reset();
    TestFalse(TEXT("Reset 后已注册段清空"), Registry.HasSegment(SegmentName));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
