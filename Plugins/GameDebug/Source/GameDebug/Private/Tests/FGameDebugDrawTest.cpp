/**
 * @file FGameDebugDrawTest.cpp
 * @brief FGameDebugDraw 3D 调试图形绘制与边界防卫真测试（基于 ULineBatchComponent::BatchedLines 精确断言增量）
 */

#include "Visualizer/FGameDebugDraw.h"

#include "Tests/GameDebugTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugDrawGeometricPrimitivesTest,
    "Game.Debug.Visualizer.GeometricPrimitives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证全部 9 种 3D 几何图元及宏在有效世界下真实向 ULineBatchComponent 写入线段（区分单帧与持续时长）
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameDebugDrawGeometricPrimitivesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    UWorld* World = Game::Debug::Test::GetAutomationTestWorld();
    TestNotNull(TEXT("自动化测试环境存在有效 UWorld"), World);
    if (World == nullptr) {
        return false;
    }

    Game::Debug::Test::FlushWorldDebugLines(World);

    auto AssertLineDeltaPositive = [this, World](const TCHAR* PrimitiveName, TFunctionRef<void()> DrawOp) {
        const int32 Before = Game::Debug::Test::GetTotalBatchedLineCount(World);
        DrawOp();
        const int32 After = Game::Debug::Test::GetTotalBatchedLineCount(World);
        TestTrue(FString::Printf(TEXT("%s 真实写入调试线段 (增量 %d)"), PrimitiveName, After - Before), After > Before);
    };

    AssertLineDeltaPositive(TEXT("DrawLine (单帧)"), [&]() {
        FGameDebugDraw::DrawLine(World, FVector::ZeroVector, FVector(100.0, 0.0, 0.0), FColor::Red, 0.0f, 2.0f);
    });
    AssertLineDeltaPositive(TEXT("DrawSphere (持续)"), [&]() {
        FGameDebugDraw::DrawSphere(World, FVector(0.0, 100.0, 0.0), 50.0f, FColor::Green, 12, 0.1f, 1.5f);
    });
    AssertLineDeltaPositive(TEXT("DrawBox"), [&]() {
        FGameDebugDraw::DrawBox(World, FVector(0.0, 0.0, 100.0), FVector(30.0, 30.0, 30.0), FQuat::Identity, FColor::Blue, 0.1f, 1.0f);
    });
    AssertLineDeltaPositive(TEXT("DrawBox (零四元数安全归一化)"), [&]() {
        FGameDebugDraw::DrawBox(World, FVector(0.0, 0.0, 120.0), FVector(20.0, 20.0, 20.0), FQuat(0.0, 0.0, 0.0, 0.0), FColor::Blue, 0.1f, 1.0f);
    });
    AssertLineDeltaPositive(TEXT("DrawCapsule"), [&]() {
        FGameDebugDraw::DrawCapsule(World, FVector(100.0, 100.0, 0.0), 60.0f, 30.0f, FQuat::Identity, FColor::Yellow, 0.1f, 1.0f);
    });
    AssertLineDeltaPositive(TEXT("DrawCapsule (非单位四元数安全归一化)"), [&]() {
        FGameDebugDraw::DrawCapsule(World, FVector(120.0, 100.0, 0.0), 60.0f, 30.0f, FQuat(2.0, 0.0, 0.0, 0.0), FColor::Yellow, 0.1f, 1.0f);
    });
    AssertLineDeltaPositive(TEXT("DrawCylinder"), [&]() {
        FGameDebugDraw::DrawCylinder(World, FVector::ZeroVector, FVector(0.0, 0.0, 100.0), 25.0f, 12, FColor::Orange, 0.1f, 1.0f);
    });
    AssertLineDeltaPositive(TEXT("DrawCone"), [&]() {
        FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ForwardVector, 150.0f, 0.5f, 0.5f, 12, FColor::Cyan, 0.1f, 1.0f);
    });
    AssertLineDeltaPositive(TEXT("DrawArrow"), [&]() {
        FGameDebugDraw::DrawArrow(World, FVector::ZeroVector, FVector(0.0, 0.0, 80.0), 20.0f, FColor::Purple, 0.1f, 2.0f);
    });
    AssertLineDeltaPositive(TEXT("DrawCoordinateSystem"), [&]() {
        FGameDebugDraw::DrawCoordinateSystem(World, FVector(50.0, 50.0, 50.0), FRotator::ZeroRotator, 40.0f, 0.1f, 1.5f);
    });

    FGameDebugDraw::DrawString(World, FVector(0.0, 0.0, 120.0), TEXT("Debug Target Point"), nullptr, FColor::White, 0.1f, true, 1.2f);

    const ULineBatchComponent* FrameBatcher = Game::Debug::Test::GetWorldDebugLineBatcher(World, false);
    const ULineBatchComponent* PersistentBatcher = Game::Debug::Test::GetWorldDebugLineBatcher(World, true);
    TestTrue(TEXT("单帧图元已写入 World LineBatcher"), FrameBatcher != nullptr && FrameBatcher->BatchedLines.Num() >= 1);
    TestTrue(TEXT("持续图元已写入 WorldPersistent LineBatcher"), PersistentBatcher != nullptr && PersistentBatcher->BatchedLines.Num() > 10);

    const int32 PreMacroCount = Game::Debug::Test::GetTotalBatchedLineCount(World);
    const bool bRunMacroBranch = (World != nullptr);
    if (bRunMacroBranch)
        GAME_DEBUG_DRAW_LINE(World, FVector::ZeroVector, FVector(50.0, 0.0, 0.0));
    else
        TestTrue(TEXT("不可达分支"), false);
    GAME_DEBUG_DRAW_SPHERE(World, FVector(0.0, 50.0, 0.0), 25.0f);
    GAME_DEBUG_DRAW_BOX(World, FVector(0.0, 0.0, 50.0), FVector(20.0, 20.0, 20.0));
    GAME_DEBUG_DRAW_CAPSULE(World, FVector(50.0, 50.0, 0.0), 40.0f, 20.0f);
    GAME_DEBUG_DRAW_CYLINDER(World, FVector::ZeroVector, FVector(0.0, 0.0, 50.0), 15.0f);
    GAME_DEBUG_DRAW_CONE(World, FVector::ZeroVector, FVector::ForwardVector, 100.0f, 0.5f, 0.5f);
    GAME_DEBUG_DRAW_ARROW(World, FVector::ZeroVector, FVector(0.0, 0.0, 40.0));
    GAME_DEBUG_DRAW_COORD(World, FVector::ZeroVector, FRotator::ZeroRotator);
    GAME_DEBUG_DRAW_STRING(World, FVector::ZeroVector, TEXT("Macro Draw Test"));

    const int32 PostMacroCount = Game::Debug::Test::GetTotalBatchedLineCount(World);
    TestTrue(TEXT("全部绘制宏真实提交线段且增量显著大于 0"), PostMacroCount > PreMacroCount + 20);

    Game::Debug::Test::FlushWorldDebugLines(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugDrawBoundarySafetyTest,
    "Game.Debug.Visualizer.BoundarySafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 调试绘制在空世界、NaN/Inf、负/零尺寸、退化线段、非法分段数与空文本边界下严格拦截（BatchedLines 增量严格为 0）
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameDebugDrawBoundarySafetyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    FGameDebugDraw::DrawLine(nullptr, FVector::ZeroVector, FVector(100.0, 0.0, 0.0));
    FGameDebugDraw::DrawSphere(nullptr, FVector::ZeroVector, 50.0f);
    FGameDebugDraw::DrawBox(nullptr, FVector::ZeroVector, FVector(50.0, 50.0, 50.0));
    FGameDebugDraw::DrawCapsule(nullptr, FVector::ZeroVector, 50.0f, 20.0f);
    FGameDebugDraw::DrawCylinder(nullptr, FVector::ZeroVector, FVector::UpVector, 20.0f);
    FGameDebugDraw::DrawCone(nullptr, FVector::ZeroVector, FVector::ForwardVector, 100.0f, 0.5f, 0.5f);
    FGameDebugDraw::DrawArrow(nullptr, FVector::ZeroVector, FVector::UpVector);
    FGameDebugDraw::DrawCoordinateSystem(nullptr, FVector::ZeroVector, FRotator::ZeroRotator);
    FGameDebugDraw::DrawString(nullptr, FVector::ZeroVector, TEXT("Test"));

    UWorld* World = Game::Debug::Test::GetAutomationTestWorld();
    TestNotNull(TEXT("自动化测试环境存在有效 UWorld"), World);
    if (World == nullptr) {
        return false;
    }

    Game::Debug::Test::FlushWorldDebugLines(World);
    const int32 BaselineLines = Game::Debug::Test::GetTotalBatchedLineCount(World);

    const float QuietNaN = std::numeric_limits<float>::quiet_NaN();
    const float PosInf = std::numeric_limits<float>::infinity();
    const FVector NaNVec(QuietNaN, 0.0, 0.0);
    const FVector InfVec(PosInf, 0.0, 0.0);

    FGameDebugDraw::DrawLine(World, NaNVec, FVector::UpVector);
    FGameDebugDraw::DrawLine(World, FVector::ZeroVector, InfVec);
    FGameDebugDraw::DrawLine(World, FVector::ZeroVector, FVector::UpVector, FColor::Red, -1.0f, 1.0f);
    FGameDebugDraw::DrawLine(World, FVector::ZeroVector, FVector::UpVector, FColor::Red, 0.0f, -2.0f);

    FGameDebugDraw::DrawSphere(World, FVector::ZeroVector, -10.0f);
    FGameDebugDraw::DrawSphere(World, FVector::ZeroVector, 0.0f);
    FGameDebugDraw::DrawSphere(World, FVector::ZeroVector, QuietNaN);
    FGameDebugDraw::DrawSphere(World, FVector::ZeroVector, PosInf);
    FGameDebugDraw::DrawSphere(World, FVector::ZeroVector, 50.0f, FColor::Cyan, 0);
    FGameDebugDraw::DrawSphere(World, NaNVec, 50.0f);

    FGameDebugDraw::DrawBox(World, FVector::ZeroVector, FVector(-5.0, 10.0, 10.0));
    FGameDebugDraw::DrawBox(World, FVector::ZeroVector, FVector(10.0, -5.0, 10.0));
    FGameDebugDraw::DrawBox(World, FVector::ZeroVector, FVector(10.0, 10.0, -5.0));
    FGameDebugDraw::DrawBox(World, FVector::ZeroVector, FVector::ZeroVector);
    FGameDebugDraw::DrawBox(World, FVector::ZeroVector, InfVec);

    FGameDebugDraw::DrawCapsule(World, FVector::ZeroVector, -50.0f, 20.0f);
    FGameDebugDraw::DrawCapsule(World, FVector::ZeroVector, 50.0f, -20.0f);
    FGameDebugDraw::DrawCapsule(World, FVector::ZeroVector, 0.0f, 0.0f);
    FGameDebugDraw::DrawCapsule(World, FVector::ZeroVector, PosInf, 20.0f);

    FGameDebugDraw::DrawCylinder(World, FVector::ZeroVector, FVector::ZeroVector, 20.0f);
    FGameDebugDraw::DrawCylinder(World, FVector::ZeroVector, FVector::UpVector, -10.0f);
    FGameDebugDraw::DrawCylinder(World, FVector::ZeroVector, FVector::UpVector, 20.0f, 0);

    FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ZeroVector, 100.0f, 0.5f, 0.5f);
    FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ForwardVector, -50.0f, 0.5f, 0.5f);
    FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ForwardVector, 100.0f, 0.0f, 0.5f);
    FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ForwardVector, 100.0f, 0.5f, -0.5f);
    FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ForwardVector, 100.0f, 0.5f, 0.5f, 0);

    FGameDebugDraw::DrawArrow(World, FVector::ZeroVector, FVector::ZeroVector, 20.0f);
    FGameDebugDraw::DrawArrow(World, FVector::ZeroVector, FVector::UpVector, -10.0f);
    FGameDebugDraw::DrawArrow(World, FVector::ZeroVector, FVector::UpVector, 0.0f);

    FGameDebugDraw::DrawCoordinateSystem(World, FVector::ZeroVector, FRotator::ZeroRotator, -20.0f);
    FGameDebugDraw::DrawCoordinateSystem(World, FVector::ZeroVector, FRotator::ZeroRotator, 0.0f);
    FGameDebugDraw::DrawCoordinateSystem(World, NaNVec, FRotator::ZeroRotator, 50.0f);

    FGameDebugDraw::DrawString(World, FVector::ZeroVector, FString());
    FGameDebugDraw::DrawString(World, FVector::ZeroVector, TEXT("InvalidScale"), nullptr, FColor::White, 0.0f, false, 0.0f);
    FGameDebugDraw::DrawString(World, NaNVec, TEXT("NaNLoc"));

    TestEqual(
        TEXT("全部非法、NaN/Inf 与退化几何参数均被严格拦截，BatchedLines 增量严格为 0"),
        Game::Debug::Test::GetTotalBatchedLineCount(World),
        BaselineLines
    );

    Game::Debug::Test::FlushWorldDebugLines(World);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
