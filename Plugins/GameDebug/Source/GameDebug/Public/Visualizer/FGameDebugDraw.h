/**
 * @file FGameDebugDraw.h
 * @brief 现代化轻量 3D 调试图形绘制工具（完整边界防卫、防 NaN/Inf 退化与编译开关零开销剥离）
 */

#pragma once

#include "CoreMinimal.h"
#include "Config/GameCompileSwitches.h"

class AActor;
class UWorld;

/**
 * @brief 3D 调试图形绘制工具类（纯静态不可实例化）
 */
class GAMEDEBUG_API FGameDebugDraw final {
public:
    FGameDebugDraw() = delete;
    ~FGameDebugDraw() = delete;

    FGameDebugDraw(const FGameDebugDraw&) = delete;
    FGameDebugDraw& operator=(const FGameDebugDraw&) = delete;
    FGameDebugDraw(FGameDebugDraw&&) = delete;
    FGameDebugDraw& operator=(FGameDebugDraw&&) = delete;

    /**
     * @brief 绘制调试线段
     * @param World 目标世界
     * @param LineStart 起点坐标（须为有限值）
     * @param LineEnd 终点坐标（须为有限值）
     * @param Color 线段颜色
     * @param Duration 持续时间（秒，0 为单帧，须为非负有限值）
     * @param Thickness 线条粗细（须为非负有限值）
     */
    static void DrawLine(
        const UWorld* World,
        const FVector& LineStart,
        const FVector& LineEnd,
        const FColor& Color = FColor::Green,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制调试球体
     * @param World 目标世界
     * @param Center 球心坐标（须为有限值）
     * @param Radius 球体半径（须为正有限值）
     * @param Color 线框颜色
     * @param Segments 细分段数（须大于 0）
     * @param Duration 持续时间（秒，0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawSphere(
        const UWorld* World,
        const FVector& Center,
        float Radius,
        const FColor& Color = FColor::Cyan,
        int32 Segments = 16,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制调试轴对齐或定向包围盒
     * @param World 目标世界
     * @param Center 中心坐标（须为有限值）
     * @param Extent 盒体半长宽（各分量非负有限且不全为零）
     * @param Rotation 盒体朝向（须为有限值，非单位四元数自动安全归一化）
     * @param Color 线框颜色
     * @param Duration 持续时间（秒，0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawBox(
        const UWorld* World,
        const FVector& Center,
        const FVector& Extent,
        const FQuat& Rotation = FQuat::Identity,
        const FColor& Color = FColor::Yellow,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制调试胶囊体
     * @param World 目标世界
     * @param Center 中心坐标（须为有限值）
     * @param HalfHeight 胶囊体半高（须为正有限值）
     * @param Radius 胶囊体半径（须为正有限值）
     * @param Rotation 旋转朝向（须为有限值，非单位四元数自动安全归一化）
     * @param Color 线框颜色
     * @param Duration 持续时间（秒，0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawCapsule(
        const UWorld* World,
        const FVector& Center,
        float HalfHeight,
        float Radius,
        const FQuat& Rotation = FQuat::Identity,
        const FColor& Color = FColor::Green,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制调试圆柱体
     * @param World 目标世界
     * @param Start 起始底面中心（须为有限值且不与 End 重合）
     * @param End 终止顶面中心（须为有限值）
     * @param Radius 底面半径（须为正有限值）
     * @param Segments 细分段数（须大于 0）
     * @param Color 线框颜色
     * @param Duration 持续时间（秒，0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawCylinder(
        const UWorld* World,
        const FVector& Start,
        const FVector& End,
        float Radius,
        int32 Segments = 16,
        const FColor& Color = FColor::Orange,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制调试圆锥/扇形视野锥
     * @param World 目标世界
     * @param Origin 锥顶点坐标（须为有限值）
     * @param Direction 锥主轴朝向矢量（须为非零有限值）
     * @param Length 锥体轴向长度（须为正有限值）
     * @param AngleWidthRad 锥体水平张角弧度（须为正有限值）
     * @param AngleHeightRad 锥体垂直张角弧度（须为正有限值）
     * @param NumSides 侧面细分线数量（须大于 0）
     * @param Color 线框颜色
     * @param Duration 持续时间（秒，0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawCone(
        const UWorld* World,
        const FVector& Origin,
        const FVector& Direction,
        float Length,
        float AngleWidthRad,
        float AngleHeightRad,
        int32 NumSides = 16,
        const FColor& Color = FColor::Yellow,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制调试方向箭头
     * @param World 目标世界
     * @param LineStart 箭头起始点（须为有限值且不与 LineEnd 重合）
     * @param LineEnd 箭头终止点（须为有限值）
     * @param ArrowSize 箭头头部尺寸（须为正有限值）
     * @param Color 线框颜色
     * @param Duration 持续时间（秒，0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawArrow(
        const UWorld* World,
        const FVector& LineStart,
        const FVector& LineEnd,
        float ArrowSize = 20.0f,
        const FColor& Color = FColor::Purple,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 绘制局部坐标轴三向线（X=红, Y=绿, Z=蓝）
     * @param World 目标世界
     * @param AxisLoc 坐标系原点（须为有限值）
     * @param AxisRot 坐标系朝向（须为有限值）
     * @param Scale 坐标轴线段长度（须为正有限值）
     * @param Duration 持续时间（秒，0 为单帧）
     * @param Thickness 线条粗细
     */
    static void DrawCoordinateSystem(
        const UWorld* World,
        const FVector& AxisLoc,
        const FRotator& AxisRot,
        float Scale = 50.0f,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 在 3D 空间世界坐标处绘制悬浮文本
     * @param World 目标世界
     * @param TextLocation 空间位置（须为有限值）
     * @param Text 呈现文本（不可为空）
     * @param TestActor 关联 Actor（可选；非空时须有效且归属目标 World）
     * @param TextColor 文本颜色
     * @param Duration 持续时间（秒，0 为单帧）
     * @param bDrawShadow 是否带有阴影
     * @param FontScale 字体缩放比例（须为正有限值）
     */
    static void DrawString(
        const UWorld* World,
        const FVector& TextLocation,
        const FString& Text,
        AActor* TestActor = nullptr,
        const FColor& TextColor = FColor::White,
        float Duration = 0.0f,
        bool bDrawShadow = false,
        float FontScale = 1.0f
    );
};

#if GAME_ENABLE_DEBUG_DRAW != 0
    #define GAME_DEBUG_DRAW_LINE(World, Start, End, ...) \
        do { \
            FGameDebugDraw::DrawLine((World), (Start), (End), ##__VA_ARGS__); \
        } while (false)

    #define GAME_DEBUG_DRAW_SPHERE(World, Center, Radius, ...) \
        do { \
            FGameDebugDraw::DrawSphere((World), (Center), (Radius), ##__VA_ARGS__); \
        } while (false)

    #define GAME_DEBUG_DRAW_BOX(World, Center, Extent, ...) \
        do { \
            FGameDebugDraw::DrawBox((World), (Center), (Extent), ##__VA_ARGS__); \
        } while (false)

    #define GAME_DEBUG_DRAW_CAPSULE(World, Center, HalfHeight, Radius, ...) \
        do { \
            FGameDebugDraw::DrawCapsule((World), (Center), (HalfHeight), (Radius), ##__VA_ARGS__); \
        } while (false)

    #define GAME_DEBUG_DRAW_CYLINDER(World, Start, End, Radius, ...) \
        do { \
            FGameDebugDraw::DrawCylinder((World), (Start), (End), (Radius), ##__VA_ARGS__); \
        } while (false)

    #define GAME_DEBUG_DRAW_CONE(World, Origin, Direction, Length, AngleWidthRad, AngleHeightRad, ...) \
        do { \
            FGameDebugDraw::DrawCone((World), (Origin), (Direction), (Length), (AngleWidthRad), (AngleHeightRad), ##__VA_ARGS__); \
        } while (false)

    #define GAME_DEBUG_DRAW_ARROW(World, Start, End, ...) \
        do { \
            FGameDebugDraw::DrawArrow((World), (Start), (End), ##__VA_ARGS__); \
        } while (false)

    #define GAME_DEBUG_DRAW_COORD(World, Loc, Rot, ...) \
        do { \
            FGameDebugDraw::DrawCoordinateSystem((World), (Loc), (Rot), ##__VA_ARGS__); \
        } while (false)

    #define GAME_DEBUG_DRAW_STRING(World, Loc, Text, ...) \
        do { \
            FGameDebugDraw::DrawString((World), (Loc), (Text), ##__VA_ARGS__); \
        } while (false)
#else
    #define GAME_DEBUG_DRAW_LINE(World, Start, End, ...) ((void)0)
    #define GAME_DEBUG_DRAW_SPHERE(World, Center, Radius, ...) ((void)0)
    #define GAME_DEBUG_DRAW_BOX(World, Center, Extent, ...) ((void)0)
    #define GAME_DEBUG_DRAW_CAPSULE(World, Center, HalfHeight, Radius, ...) ((void)0)
    #define GAME_DEBUG_DRAW_CYLINDER(World, Start, End, Radius, ...) ((void)0)
    #define GAME_DEBUG_DRAW_CONE(World, Origin, Direction, Length, AngleWidthRad, AngleHeightRad, ...) ((void)0)
    #define GAME_DEBUG_DRAW_ARROW(World, Start, End, ...) ((void)0)
    #define GAME_DEBUG_DRAW_COORD(World, Loc, Rot, ...) ((void)0)
    #define GAME_DEBUG_DRAW_STRING(World, Loc, Text, ...) ((void)0)
#endif
