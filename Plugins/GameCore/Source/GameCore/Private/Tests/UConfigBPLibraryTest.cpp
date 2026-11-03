/**
 * @file UConfigBPLibraryTest.cpp
 * @brief 配置注册表蓝图函数库自动化测试（全部支持类型读写与回退对账），Shipping 构建自动剔除
 */

#include "Config/UConfigBPLibrary.h"

#if WITH_AUTOMATION_TESTS

#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameConfigBPLibraryDefaultsTest, "Game.Core.Config.BPLibrary.Defaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证未注册键按调用方入参返回回退默认值
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameConfigBPLibraryDefaultsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FName MissingKey(TEXT("UnitTest.BP.MissingKey"));

    TestFalse(TEXT("未注册键 IsConfigRegistered 返回 false"), UConfigBPLibrary::IsConfigRegistered(MissingKey));
    TestTrue(TEXT("布尔默认回退"), UConfigBPLibrary::GetConfigBool(MissingKey, true));
    TestEqual(TEXT("32位整型默认回退"), UConfigBPLibrary::GetConfigInt(MissingKey, 123), 123);
    TestEqual(TEXT("64位整型默认回退"), UConfigBPLibrary::GetConfigInt64(MissingKey, 456LL), 456LL);
    TestNearlyEqual(TEXT("单精度浮点默认回退"), UConfigBPLibrary::GetConfigFloat(MissingKey, 3.14f), 3.14f, 0.0001f);
    TestNearlyEqual(TEXT("双精度浮点默认回退"), UConfigBPLibrary::GetConfigDouble(MissingKey, 6.28), 6.28, 0.0001);
    TestEqual(TEXT("字符串默认回退"), UConfigBPLibrary::GetConfigString(MissingKey, TEXT("DefaultStr")), TEXT("DefaultStr"));
    TestEqual(TEXT("名称默认回退"), UConfigBPLibrary::GetConfigName(MissingKey, FName(TEXT("DefaultName"))), FName(TEXT("DefaultName")));

    const FVector DefaultVec(1.0, 2.0, 3.0);
    TestTrue(TEXT("三维向量默认回退"), UConfigBPLibrary::GetConfigVector(MissingKey, DefaultVec).Equals(DefaultVec));

    const FRotator DefaultRot(10.0, 20.0, 30.0);
    TestTrue(TEXT("旋转体默认回退"), UConfigBPLibrary::GetConfigRotator(MissingKey, DefaultRot).Equals(DefaultRot));

    const FGameplayTag DefaultTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Game.Core.UnitTest.BPTagDefault")));
    TestTrue(TEXT("GameplayTag 默认回退"), UConfigBPLibrary::GetConfigGameplayTag(MissingKey, DefaultTag) == DefaultTag);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameConfigBPLibrarySetAndGetTest, "Game.Core.Config.BPLibrary.SetAndGet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证经蓝图函数库写入全部类型配置后均可准确回读
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FGameConfigBPLibrarySetAndGetTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FName BoolKey(TEXT("UnitTest.BP.SetGet.Bool"));
    const FName IntKey(TEXT("UnitTest.BP.SetGet.Int"));
    const FName Int64Key(TEXT("UnitTest.BP.SetGet.Int64"));
    const FName FloatKey(TEXT("UnitTest.BP.SetGet.Float"));
    const FName DoubleKey(TEXT("UnitTest.BP.SetGet.Double"));
    const FName StringKey(TEXT("UnitTest.BP.SetGet.String"));
    const FName NameKey(TEXT("UnitTest.BP.SetGet.Name"));
    const FName VectorKey(TEXT("UnitTest.BP.SetGet.Vector"));
    const FName RotatorKey(TEXT("UnitTest.BP.SetGet.Rotator"));
    const FName TagKey(TEXT("UnitTest.BP.SetGet.Tag"));

    UConfigBPLibrary::SetConfigBool(BoolKey, true);
    UConfigBPLibrary::SetConfigInt(IntKey, 999);
    UConfigBPLibrary::SetConfigInt64(Int64Key, 88888888LL);
    UConfigBPLibrary::SetConfigFloat(FloatKey, 12.34f);
    UConfigBPLibrary::SetConfigDouble(DoubleKey, 56.78);
    UConfigBPLibrary::SetConfigString(StringKey, TEXT("UpdatedString"));
    UConfigBPLibrary::SetConfigName(NameKey, FName(TEXT("UpdatedName")));

    const FVector TargetVector(10.0, -20.0, 30.0);
    const FRotator TargetRotator(45.0, 90.0, 0.0);
    const FGameplayTag TargetTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Game.Core.UnitTest.BPTagTarget")));

    UConfigBPLibrary::SetConfigVector(VectorKey, TargetVector);
    UConfigBPLibrary::SetConfigRotator(RotatorKey, TargetRotator);
    UConfigBPLibrary::SetConfigGameplayTag(TagKey, TargetTag);

    TestTrue(TEXT("布尔已注册"), UConfigBPLibrary::IsConfigRegistered(BoolKey));
    TestTrue(TEXT("布尔回读准确"), UConfigBPLibrary::GetConfigBool(BoolKey, false));
    TestEqual(TEXT("32位整型回读准确"), UConfigBPLibrary::GetConfigInt(IntKey, 0), 999);
    TestEqual(TEXT("64位整型回读准确"), UConfigBPLibrary::GetConfigInt64(Int64Key, 0LL), 88888888LL);
    TestNearlyEqual(TEXT("单精度浮点回读准确"), UConfigBPLibrary::GetConfigFloat(FloatKey, 0.0f), 12.34f, 0.0001f);
    TestNearlyEqual(TEXT("双精度浮点回读准确"), UConfigBPLibrary::GetConfigDouble(DoubleKey, 0.0), 56.78, 0.0001);
    TestEqual(TEXT("字符串回读准确"), UConfigBPLibrary::GetConfigString(StringKey, TEXT("")), TEXT("UpdatedString"));
    TestEqual(TEXT("名称回读准确"), UConfigBPLibrary::GetConfigName(NameKey, NAME_None), FName(TEXT("UpdatedName")));
    TestTrue(TEXT("三维向量回读准确"), UConfigBPLibrary::GetConfigVector(VectorKey, FVector::ZeroVector).Equals(TargetVector));
    TestTrue(TEXT("旋转体回读准确"), UConfigBPLibrary::GetConfigRotator(RotatorKey, FRotator::ZeroRotator).Equals(TargetRotator));
    TestTrue(TEXT("GameplayTag 回读准确"), UConfigBPLibrary::GetConfigGameplayTag(TagKey, FGameplayTag::EmptyTag) == TargetTag);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

