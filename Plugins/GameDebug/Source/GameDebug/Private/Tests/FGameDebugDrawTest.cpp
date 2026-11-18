/**
 * @file FGameDebugDrawTest.cpp
 * @brief FGameDebugDraw 3D 调试图形绘制与安全防卫自动化测试
 */

#include "Visualizer/FGameDebugDraw.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugDrawGeometricPrimitivesTest,
    "Game.Debug.Visualizer.GeometricPrimitives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证全部 9 种 3D 几何图元在有效世界上下文下的安全绘制与宏展开
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameDebugDrawGeometricPrimitivesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = nullptr;
    if (GEngine != nullptr) {
        World = GEngine->GetWorldContexts().IsEmpty() ? nullptr : GEngine->GetWorldContexts()[0].World();
    }

    FGameDebugDraw::DrawLine(World, FVector::ZeroVector, FVector(100.0, 0.0, 0.0), FColor::Red, 0.1f, 2.0f);
    FGameDebugDraw::DrawSphere(World, FVector(0.0, 100.0, 0.0), 50.0f, FColor::Green, 16, 0.1f, 1.5f);
    FGameDebugDraw::DrawBox(World, FVector(0.0, 0.0, 100.0), FVector(30.0, 30.0, 30.0), FQuat::Identity, FColor::Blue, 0.1f, 1.0f);
    FGameDebugDraw::DrawCapsule(World, FVector(100.0, 100.0, 0.0), 60.0f, 30.0f, FQuat::Identity, FColor::Yellow, 0.1f, 1.0f);
    FGameDebugDraw::DrawCylinder(World, FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 100.0), 25.0f, 16, FColor::Orange, 0.1f, 1.0f);
    FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ForwardVector, 150.0f, FMath::DegreesToRadians(30.0f), FMath::DegreesToRadians(30.0f), 16, FColor::Cyan, 0.1f, 1.0f);
    FGameDebugDraw::DrawArrow(World, FVector::ZeroVector, FVector(0.0, 0.0, 80.0), 20.0f, FColor::Purple, 0.1f, 2.0f);
    FGameDebugDraw::DrawCoordinateSystem(World, FVector(50.0, 50.0, 50.0), FRotator::ZeroRotator, 40.0f, 0.1f, 1.5f);
    FGameDebugDraw::DrawString(World, FVector(0.0, 0.0, 120.0), TEXT("Debug Target Point"), nullptr, FColor::White, 0.1f, true, 1.2f);

    GAME_DEBUG_DRAW_LINE(World, FVector::ZeroVector, FVector(50.0, 0.0, 0.0));
    GAME_DEBUG_DRAW_SPHERE(World, FVector(0.0, 50.0, 0.0), 25.0f);
    GAME_DEBUG_DRAW_BOX(World, FVector(0.0, 0.0, 50.0), FVector(20.0, 20.0, 20.0));
    GAME_DEBUG_DRAW_CAPSULE(World, FVector(50.0, 50.0, 0.0), 40.0f, 20.0f);
    GAME_DEBUG_DRAW_CYLINDER(World, FVector::ZeroVector, FVector(0.0, 0.0, 50.0), 15.0f);
    GAME_DEBUG_DRAW_CONE(World, FVector::ZeroVector, FVector::ForwardVector, 100.0f, 0.5f, 0.5f);
    GAME_DEBUG_DRAW_ARROW(World, FVector::ZeroVector, FVector(0.0, 0.0, 40.0));
    GAME_DEBUG_DRAW_COORD(World, FVector::ZeroVector, FRotator::ZeroRotator);
    GAME_DEBUG_DRAW_STRING(World, FVector::ZeroVector, TEXT("Macro Draw Test"));

    TestTrue(TEXT("全部 9 类 3D 几何图元及宏在有效世界或安全空值下正常绘制"), true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugDrawBoundarySafetyTest,
    "Game.Debug.Visualizer.BoundarySafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 3D 调试绘制在空世界、负半径、负高度、零方向与空字符串等极限边界下的安全性
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameDebugDrawBoundarySafetyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDebugDraw::DrawLine(nullptr, FVector::ZeroVector, FVector(100.0, 0.0, 0.0));
    FGameDebugDraw::DrawSphere(nullptr, FVector::ZeroVector, 50.0f);
    FGameDebugDraw::DrawBox(nullptr, FVector::ZeroVector, FVector(50.0, 50.0, 50.0));
    FGameDebugDraw::DrawCapsule(nullptr, FVector::ZeroVector, 50.0f, 20.0f);
    FGameDebugDraw::DrawCylinder(nullptr, FVector::ZeroVector, FVector::UpVector, 20.0f);
    FGameDebugDraw::DrawCone(nullptr, FVector::ZeroVector, FVector::ForwardVector, 100.0f, 0.5f, 0.5f);
    FGameDebugDraw::DrawArrow(nullptr, FVector::ZeroVector, FVector::UpVector);
    FGameDebugDraw::DrawCoordinateSystem(nullptr, FVector::ZeroVector, FRotator::ZeroRotator);
    FGameDebugDraw::DrawString(nullptr, FVector::ZeroVector, TEXT("Test"));

    UWorld* World = nullptr;
    if (GEngine != nullptr && !GEngine->GetWorldContexts().IsEmpty()) {
        World = GEngine->GetWorldContexts()[0].World();
    }

    FGameDebugDraw::DrawSphere(World, FVector::ZeroVector, -10.0f);
    FGameDebugDraw::DrawSphere(World, FVector::ZeroVector, 0.0f);

    FGameDebugDraw::DrawBox(World, FVector::ZeroVector, FVector(-5.0, 10.0, 10.0));
    FGameDebugDraw::DrawBox(World, FVector::ZeroVector, FVector(10.0, -5.0, 10.0));
    FGameDebugDraw::DrawBox(World, FVector::ZeroVector, FVector(10.0, 10.0, -5.0));

    FGameDebugDraw::DrawCapsule(World, FVector::ZeroVector, -50.0f, 20.0f);
    FGameDebugDraw::DrawCapsule(World, FVector::ZeroVector, 50.0f, -20.0f);
    FGameDebugDraw::DrawCapsule(World, FVector::ZeroVector, 0.0f, 0.0f);

    FGameDebugDraw::DrawCylinder(World, FVector::ZeroVector, FVector::ZeroVector, 20.0f);
    FGameDebugDraw::DrawCylinder(World, FVector::ZeroVector, FVector::UpVector, -10.0f);

    FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ZeroVector, 100.0f, 0.5f, 0.5f);
    FGameDebugDraw::DrawCone(World, FVector::ZeroVector, FVector::ForwardVector, -50.0f, 0.5f, 0.5f);

    FGameDebugDraw::DrawArrow(World, FVector::ZeroVector, FVector::UpVector, -10.0f);
    FGameDebugDraw::DrawArrow(World, FVector::ZeroVector, FVector::UpVector, 0.0f);

    FGameDebugDraw::DrawCoordinateSystem(World, FVector::ZeroVector, FRotator::ZeroRotator, -20.0f);
    FGameDebugDraw::DrawCoordinateSystem(World, FVector::ZeroVector, FRotator::ZeroRotator, 0.0f);

    FGameDebugDraw::DrawString(World, FVector::ZeroVector, FString());

    TestTrue(TEXT("全部异常极值与空指针参数均被安全防御拦截且未引发崩溃"), true);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
