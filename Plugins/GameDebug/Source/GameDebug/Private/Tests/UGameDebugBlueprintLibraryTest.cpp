/**
 * @file UGameDebugBlueprintLibraryTest.cpp
 * @brief UGameDebugBlueprintLibrary 全量蓝图打印、3D 绘制与时间控制节点真测试
 */

#include "Library/UGameDebugBlueprintLibrary.h"

#include "Config/GameCompileSwitches.h"
#include "Print/FGameDebugPrint.h"
#include "Tests/GameDebugTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugBlueprintLibraryTest,
    "Game.Debug.Library.BlueprintFunctions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 UGameDebugBlueprintLibrary 暴露的全部 20 个蓝图节点的真实屏幕输出、线段批处理写入、默认颜色对齐与时间控制副作用
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugBlueprintLibraryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    UWorld* World = Game::Debug::Test::GetAutomationTestWorld();
    TestNotNull(TEXT("自动化测试环境存在有效 UWorld"), World);
    TestNotNull(TEXT("自动化测试环境 GEngine 有效"), GEngine);
    if (World == nullptr || GEngine == nullptr) {
        return false;
    }

    Game::Debug::Test::FScopedPrintCapture PrintCapture;
    Game::Debug::Test::FlushWorldDebugLines(World);

    const FName BpChannel(TEXT("BPCombat"));
    const int32 BaseInvalidCount = PrintCapture.CountByKey(FGameDebugPrint::InvalidKey);

    UGameDebugBlueprintLibrary::PrintDebugMessage(TEXT("BP Base Message"), FColor::Cyan, 1.0f, -1);
    UGameDebugBlueprintLibrary::PrintChannelMessage(BpChannel, TEXT("BP Channel Damage"), FColor::Yellow, 1.0f);
    UGameDebugBlueprintLibrary::PrintWarning(TEXT("BP Warning"), 2.0f);
    UGameDebugBlueprintLibrary::PrintError(TEXT("BP Error"), 2.0f);

    TestEqual(TEXT("3 个追加型蓝图打印节点均派发 InvalidKey 消息"), PrintCapture.CountByKey(FGameDebugPrint::InvalidKey), BaseInvalidCount + 3);
    const uint64 BpChannelKey = FGameDebugPrint::GetChannelKey(BpChannel);
    TestTrue(TEXT("蓝图通道打印节点已写入引擎屏幕槽位"), Game::Debug::Test::HasKeyedScreenMessage(BpChannelKey));
    Game::Debug::Test::FCapturedPrintRecord BpChannelMsg;
    TestTrue(TEXT("捕获到蓝图通道打印记录"), PrintCapture.FindLastByKey(BpChannelKey, BpChannelMsg));
    TestEqual(TEXT("蓝图通道正文格式正确"), BpChannelMsg.Message, FString(TEXT("[BPCombat] BP Channel Damage")));

    UGameDebugBlueprintLibrary::DrawDebugLine(World, FVector::ZeroVector, FVector(100.0, 0.0, 0.0));
    UGameDebugBlueprintLibrary::DrawDebugSphere(World, FVector::ZeroVector, 40.0f);
    UGameDebugBlueprintLibrary::DrawDebugBox(World, FVector::ZeroVector, FVector(30.0, 30.0, 30.0));
    UGameDebugBlueprintLibrary::DrawDebugCapsule(World, FVector::ZeroVector, 60.0f, 25.0f);
    UGameDebugBlueprintLibrary::DrawDebugCone(World, FVector::ZeroVector, FVector::ForwardVector, 120.0f, 30.0f, 30.0f);
    UGameDebugBlueprintLibrary::DrawDebugCoordinateSystem(World, FVector::ZeroVector, FRotator::ZeroRotator);
    UGameDebugBlueprintLibrary::DrawDebugString(World, FVector::ZeroVector, TEXT("BP Floating String"));
    TestTrue(TEXT("蓝图几何绘制节点真实写入线段"), Game::Debug::Test::GetTotalBatchedLineCount(World) > 20);

    Game::Debug::Test::FlushWorldDebugLines(World);
    UGameDebugBlueprintLibrary::DrawDebugCylinder(World, FVector::ZeroVector, FVector::UpVector * 80.0, 30.0f);
    const ULineBatchComponent* FrameBatcher = Game::Debug::Test::GetWorldDebugLineBatcher(World, false);
    TestNotNull(TEXT("World LineBatcher 有效"), FrameBatcher);
    if (FrameBatcher != nullptr && !FrameBatcher->BatchedLines.IsEmpty()) {
        TestEqual(
            TEXT("蓝图 DrawDebugCylinder 默认颜色与 C++ 对齐为 Orange"),
            FrameBatcher->BatchedLines[0].Color.ToFColor(true),
            FColor::Orange
        );
    }

    Game::Debug::Test::FlushWorldDebugLines(World);
    UGameDebugBlueprintLibrary::DrawDebugArrow(World, FVector::ZeroVector, FVector::UpVector * 60.0);
    if (FrameBatcher != nullptr && !FrameBatcher->BatchedLines.IsEmpty()) {
        TestEqual(
            TEXT("蓝图 DrawDebugArrow 默认颜色与 C++ 对齐为 Purple"),
            FrameBatcher->BatchedLines[0].Color.ToFColor(true),
            FColor::Purple
        );
    }

    Game::Debug::Test::FlushWorldDebugLines(World);
    const UObject* InvalidWorldCtx = GetTransientPackage();
    UGameDebugBlueprintLibrary::DrawDebugLine(InvalidWorldCtx, FVector::ZeroVector, FVector(100.0, 0.0, 0.0));
    TestEqual(TEXT("传入无世界上下文对象时蓝图绘制严格不写入其他世界"), Game::Debug::Test::GetTotalBatchedLineCount(World), 0);

    TestTrue(TEXT("蓝图设置时间膨胀率 0.5x 成功"), UGameDebugBlueprintLibrary::SetGlobalTimeDilation(World, 0.5f));
    TestEqual(TEXT("蓝图读取时间膨胀率为 0.5f"), UGameDebugBlueprintLibrary::GetGlobalTimeDilation(World), 0.5f);
    TestTrue(TEXT("蓝图暂停成功"), UGameDebugBlueprintLibrary::PauseGameTime(World));
    TestTrue(TEXT("蓝图 IsGamePaused 返回 true"), UGameDebugBlueprintLibrary::IsGamePaused(World));
    TestTrue(TEXT("蓝图步进 2 帧成功"), UGameDebugBlueprintLibrary::StepGameFrames(World, 2));
    TestFalse(TEXT("步进启动后退出暂停态"), UGameDebugBlueprintLibrary::IsGamePaused(World));
    TestTrue(TEXT("蓝图恢复成功"), UGameDebugBlueprintLibrary::ResumeGameTime(World));
    TestEqual(TEXT("蓝图恢复至 0.5x"), UGameDebugBlueprintLibrary::GetGlobalTimeDilation(World), 0.5f);
    TestTrue(TEXT("蓝图重置时钟成功"), UGameDebugBlueprintLibrary::ResetGameTime(World));
    TestEqual(TEXT("蓝图重置后时钟为 1.0f"), UGameDebugBlueprintLibrary::GetGlobalTimeDilation(World), 1.0f);

    Game::Debug::Test::FlushWorldDebugLines(World);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
