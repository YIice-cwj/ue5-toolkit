/**
 * @file FSaveGameSegmentRegistryTest.cpp
 * @brief 存档分段注册表自动化测试（注册契约、版本迁移、未知段保留与跨槽位生命周期）
 */

#include "Save/FSaveGameSegmentRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSegmentRegistryTest,
    "Game.Data.Save.SegmentRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证分段注册表契约校验、归档生成/应用、未知段保留与复位
 * @param Parameters 自动化测试参数
 * @return 测试框架入口固定返回 true
 */
bool FSaveGameSegmentRegistryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FSaveGameSegmentRegistry Registry;

    AddExpectedErrorPlain(TEXT("存档段注册被拒"), EAutomationExpectedErrorFlags::Contains, 3);

    FGameSaveSegment InvalidNone;
    InvalidNone.SegmentName = NAME_None;
    InvalidNone.Save = [](FArchive&) {};
    InvalidNone.Load = [](FArchive&, int32) { return true; };
    Registry.RegisterSegment(InvalidNone);
    TestFalse(TEXT("空段名注册被拒"), Registry.HasSegment(NAME_None));

    FGameSaveSegment InvalidNoSave;
    InvalidNoSave.SegmentName = FName(TEXT("NoSave"));
    InvalidNoSave.Load = [](FArchive&, int32) { return true; };
    Registry.RegisterSegment(InvalidNoSave);
    TestFalse(TEXT("空保存委托段注册被拒"), Registry.HasSegment(FName(TEXT("NoSave"))));

    FGameSaveSegment InvalidNoLoad;
    InvalidNoLoad.SegmentName = FName(TEXT("NoLoad"));
    InvalidNoLoad.Save = [](FArchive&) {};
    Registry.RegisterSegment(InvalidNoLoad);
    TestFalse(TEXT("空加载委托段注册被拒"), Registry.HasSegment(FName(TEXT("NoLoad"))));

    TestFalse(TEXT("未注册段注销返回 false"), Registry.UnregisterSegment(FName(TEXT("Missing"))));
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
    Registry.RegisterSegment(ValidSegment);
    TestTrue(TEXT("合法段注册成功"), Registry.HasSegment(SegmentName));

    const FString SlotName = TEXT("Slot_RegistryTest");
    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>();
    TestTrue(TEXT("归档构建成功"), Registry.BuildArchive(SlotName, *Archive));
    TestTrue(TEXT("归档包含目标段"), Archive->Segments.Contains(SegmentName));

    TestTrue(TEXT("归档应用成功"), Registry.ApplyArchive(SlotName, *Archive));
    TestEqual(TEXT("反序列化值一致"), LoadedValue, 42);

    UGameSaveArchive* FutureArchive = NewObject<UGameSaveArchive>();
    FGameSaveSegmentPayload FuturePayload;
    FuturePayload.SegmentVersion = 99;
    FMemoryWriter Writer(FuturePayload.Bytes);
    int32 Dummy = 100;
    Writer << Dummy;
    FutureArchive->Segments.Add(SegmentName, MoveTemp(FuturePayload));

    AddExpectedErrorPlain(TEXT("高于当前段版本"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("高版本段拒绝应用"), Registry.ApplyArchive(SlotName, *FutureArchive));

    UGameSaveArchive* UnknownArchive = NewObject<UGameSaveArchive>();
    const FName UnknownSegmentName(TEXT("UnknownModule"));
    FGameSaveSegmentPayload UnknownPayload;
    UnknownPayload.SegmentVersion = 1;
    FMemoryWriter UnknownWriter(UnknownPayload.Bytes);
    int32 UnknownData = 999;
    UnknownWriter << UnknownData;
    UnknownArchive->Segments.Add(UnknownSegmentName, MoveTemp(UnknownPayload));

    TestTrue(TEXT("包含未知段归档应用成功"), Registry.ApplyArchive(SlotName, *UnknownArchive));

    UGameSaveArchive* RebuiltArchive = NewObject<UGameSaveArchive>();
    TestTrue(TEXT("重新构建归档成功"), Registry.BuildArchive(SlotName, *RebuiltArchive));
    TestTrue(TEXT("重新构建归档保留了未知段"), RebuiltArchive->Segments.Contains(UnknownSegmentName));

    Registry.ClearPreservedSegmentsForSlot(SlotName);
    UGameSaveArchive* ClearedArchive = NewObject<UGameSaveArchive>();
    TestTrue(TEXT("清理保留段后重新构建归档成功"), Registry.BuildArchive(SlotName, *ClearedArchive));
    TestFalse(TEXT("清理保留段后未知段不再输出"), ClearedArchive->Segments.Contains(UnknownSegmentName));

    TestTrue(TEXT("注销段成功"), Registry.UnregisterSegment(SegmentName));
    TestFalse(TEXT("注销后段不存在"), Registry.HasSegment(SegmentName));

    Registry.RegisterSegment(ValidSegment);
    Registry.Reset();
    TestFalse(TEXT("Reset 后已注册段清空"), Registry.HasSegment(SegmentName));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
