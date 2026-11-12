/**
 * @file FConfigSourceBridgeTest.cpp
 * @brief FConfigSourceBridge 外部配置源桥接器自动化测试（已注册类型自动推导、显式类型标签、INI 节加载、命令行解析与只读边界拦截）
 */

#include "Config/FConfigSourceBridge.h"

#include "Config/FConfigRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameConfigSourceBridgeParseAndInjectTest,
    "Game.Core.Config.SourceBridgeParseAndInject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证已注册类型自动匹配、全量显式类型前缀、命令行引号解析、INI 节读取及只读/坏格式边界拦截
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameConfigSourceBridgeParseAndInjectTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry& Registry = FConfigRegistry::Get();

    const FName RegFloatKey(TEXT("UnitTest.Bridge.RegFloat"));
    const FName RegBoolKey(TEXT("UnitTest.Bridge.RegBool"));
    const FName RegIntKey(TEXT("UnitTest.Bridge.RegInt"));
    const FName RegDoubleKey(TEXT("UnitTest.Bridge.RegDouble"));
    const FName RegNameKey(TEXT("UnitTest.Bridge.RegName"));
    const FGameplayTag NativeTag = UGameplayTagsManager::Get().AddNativeGameplayTag(
        FName(TEXT("Game.Core.UnitTest.BridgeTag")));

    Registry.RegisterConfig<float>(RegFloatKey, 0.1f);
    Registry.RegisterConfig<bool>(RegBoolKey, false);
    Registry.RegisterConfig<int32>(RegIntKey, 10);
    Registry.RegisterConfig<double>(RegDoubleKey, 1.0);
    Registry.RegisterConfig<FName>(RegNameKey, FName(TEXT("Default")));

    TMap<FString, FString> BatchEntries;
    BatchEntries.Add(TEXT("UnitTest.Bridge.RegFloat"), TEXT("0.75"));
    BatchEntries.Add(TEXT("UnitTest.Bridge.RegBool"), TEXT("true"));
    BatchEntries.Add(TEXT("UnitTest.Bridge.RegInt"), TEXT("256"));
    BatchEntries.Add(TEXT("UnitTest.Bridge.RegDouble"), TEXT("3.1415926"));
    BatchEntries.Add(TEXT("UnitTest.Bridge.RegName"), TEXT("ArenaMap"));
    BatchEntries.Add(TEXT("int64:UnitTest.Bridge.ExplicitInt64"), TEXT("9000000000"));
    BatchEntries.Add(TEXT("vector:UnitTest.Bridge.ExplicitVec"), TEXT("X=10.0 Y=20.5 Z=-5.0"));
    BatchEntries.Add(TEXT("rotator:UnitTest.Bridge.ExplicitRot"), TEXT("P=15.0 Y=45.0 R=90.0"));
    BatchEntries.Add(TEXT("tag:UnitTest.Bridge.ExplicitTag"), TEXT("Game.Core.UnitTest.BridgeTag"));
    BatchEntries.Add(TEXT("UnitTest.Bridge.UnregisteredDefaultStr"), TEXT("PlainTextValue"));

    const int32 BatchApplied = FConfigSourceBridge::ApplyKeyValueMap(BatchEntries);
    TestEqual(TEXT("10 条合法键值全部注入成功"), BatchApplied, 10);

    TestTrue(TEXT("已注册 float 自动推导写入精确"),
        FMath::IsNearlyEqual(Registry.GetConfig<float>(RegFloatKey, 0.0f), 0.75f, 1e-5f));
    TestTrue(TEXT("已注册 bool 自动推导写入为 true"), Registry.GetConfig<bool>(RegBoolKey, false));
    TestEqual(TEXT("已注册 int32 自动推导写入为 256"), Registry.GetConfig<int32>(RegIntKey, 0), 256);
    TestTrue(TEXT("已注册 double 自动推导写入精确"),
        FMath::IsNearlyEqual(Registry.GetConfig<double>(RegDoubleKey, 0.0), 3.1415926, 1e-7));
    TestEqual(TEXT("已注册 FName 自动推导写入一致"),
        Registry.GetConfig<FName>(RegNameKey, NAME_None), FName(TEXT("ArenaMap")));
    TestEqual(TEXT("显式 int64 标签解析正确"),
        Registry.GetConfig<int64>(FName(TEXT("UnitTest.Bridge.ExplicitInt64")), 0LL), 9000000000LL);
    TestTrue(TEXT("显式 FVector 标签解析正确"),
        Registry.GetConfig<FVector>(FName(TEXT("UnitTest.Bridge.ExplicitVec")), FVector::ZeroVector)
            .Equals(FVector(10.0, 20.5, -5.0), 1e-3));
    TestTrue(TEXT("显式 FRotator 标签解析正确"),
        Registry.GetConfig<FRotator>(FName(TEXT("UnitTest.Bridge.ExplicitRot")), FRotator::ZeroRotator)
            .Equals(FRotator(15.0, 45.0, 90.0), 1e-3));
    TestEqual(TEXT("显式 FGameplayTag 标签解析正确"),
        Registry.GetConfig<FGameplayTag>(FName(TEXT("UnitTest.Bridge.ExplicitTag")), FGameplayTag::EmptyTag), NativeTag);
    TestEqual(TEXT("未注册无前缀键默认按 FString 落库"),
        Registry.GetConfig<FString>(FName(TEXT("UnitTest.Bridge.UnregisteredDefaultStr")), TEXT("")),
        FString(TEXT("PlainTextValue")));

    const TCHAR* CmdLine = TEXT(
        "-Log -Config.UnitTest.Bridge.RegInt=512 --Config.string:UnitTest.Bridge.Motd=\"Hello UE5 World\" "
        "-Config.Env.IsEditor=false -OtherArg=99");
    const int32 CliApplied = FConfigSourceBridge::ApplyCommandLineOverrides(CmdLine, TEXT("Config."));
    TestEqual(TEXT("命令行仅成功应用 2 条合法非只读覆盖项"), CliApplied, 2);
    TestEqual(TEXT("命令行覆盖 int32 生效"), Registry.GetConfig<int32>(RegIntKey, 0), 512);
    TestEqual(TEXT("命令行带空格双引号字符串完整解析并剥离外层引号"),
        Registry.GetConfig<FString>(FName(TEXT("UnitTest.Bridge.Motd")), TEXT("")),
        FString(TEXT("Hello UE5 World")));

    TestFalse(TEXT("外部尝试覆盖 Env.* 只读键在边界被拦截且不触发 ensure"),
        FConfigSourceBridge::ApplySingleEntry(TEXT("Env.BridgeReadOnlyAttempt"), TEXT("true")));
    TestFalse(TEXT("非法数值格式返回 false"),
        FConfigSourceBridge::ApplySingleEntry(TEXT("int32:UnitTest.Bridge.BadInt"), TEXT("NotANumber")));
    TestFalse(TEXT("未知显式类型前缀返回 false"),
        FConfigSourceBridge::ApplySingleEntry(TEXT("unknown_type:UnitTest.Bridge.Key"), TEXT("123")));
    TestFalse(TEXT("空键名返回 false"),
        FConfigSourceBridge::ApplySingleEntry(TEXT("int32:"), TEXT("123")));
    TestFalse(TEXT("未注册 GameplayTag 字符串返回 false"),
        FConfigSourceBridge::ApplySingleEntry(TEXT("tag:UnitTest.Bridge.InvalidTag"), TEXT("NonExistent.Tag.X99")));
    TestEqual(TEXT("空命令行返回 0"), FConfigSourceBridge::ApplyCommandLineOverrides(nullptr), 0);

    if (GConfig != nullptr && !GGameIni.IsEmpty()) {
        const FString TestSection = TEXT("UnitTest.GameCore.ConfigSourceBridgeSection");
        GConfig->SetString(*TestSection, TEXT("; CommentKey"), TEXT("IgnoredValue"), GGameIni);
        GConfig->SetString(*TestSection, TEXT("# HashCommentKey"), TEXT("IgnoredValue"), GGameIni);
        GConfig->SetString(*TestSection, TEXT("// SlashCommentKey"), TEXT("IgnoredValue"), GGameIni);
        GConfig->SetString(*TestSection, TEXT("int32:UnitTest.Bridge.IniPort"), TEXT("7788"), GGameIni);
        GConfig->SetString(*TestSection, TEXT("bool:UnitTest.Bridge.IniVerbose"), TEXT("true"), GGameIni);

        const int32 IniApplied = FConfigSourceBridge::LoadIniSection(GGameIni, TestSection);
        TestEqual(TEXT("INI 节成功读取并过滤注释后仅注入 2 条配置"), IniApplied, 2);
        TestFalse(TEXT("分号注释行未作为配置项注入"), Registry.IsRegistered(FName(TEXT("; CommentKey"))));
        TestFalse(TEXT("井号注释行未作为配置项注入"), Registry.IsRegistered(FName(TEXT("# HashCommentKey"))));
        TestFalse(TEXT("双斜杠注释行未作为配置项注入"), Registry.IsRegistered(FName(TEXT("// SlashCommentKey"))));
        TestEqual(TEXT("INI 注入端口值一致"),
            Registry.GetConfig<int32>(FName(TEXT("UnitTest.Bridge.IniPort")), 0), 7788);
        TestTrue(TEXT("INI 注入布尔值一致"),
            Registry.GetConfig<bool>(FName(TEXT("UnitTest.Bridge.IniVerbose")), false));

        GConfig->EmptySection(*TestSection, GGameIni);
    }

    if (GEngine != nullptr) {
        UGameInstance* GI = NewObject<UGameInstance>(GEngine);
        GI->InitializeStandalone(FName(TEXT("BridgeWorldContextTest")));
        if (UWorld* World = GI->GetWorld()) {
            const FName IsolatedKey(TEXT("UnitTest.Bridge.WorldSpecificKey"));
            const bool bAppliedToWorld = FConfigSourceBridge::ApplySingleEntry(
                TEXT("int32:UnitTest.Bridge.WorldSpecificKey"), TEXT("12345"), World);
            TestTrue(TEXT("向隔离世界上下文成功注入配置"), bAppliedToWorld);
            TestEqual(TEXT("隔离世界上下文正确读取注入值"),
                FConfigRegistry::Get(World).GetConfig<int32>(IsolatedKey, 0), 12345);
            TestEqual(TEXT("全局根注册表未受隔离写入污染"),
                Registry.GetConfig<int32>(IsolatedKey, 0), 0);
        }
        GI->Shutdown();
        FConfigRegistry::ClearWorldRegistriesForTesting();
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
