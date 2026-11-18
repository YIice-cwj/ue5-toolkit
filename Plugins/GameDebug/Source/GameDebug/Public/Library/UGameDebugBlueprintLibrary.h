/**
 * @file UGameDebugBlueprintLibrary.h
 * @brief GameDebug 调试与诊断蓝图函数库（向蓝图、策划与 TA 完整开放通道打印、3D 图元绘制与时间控制）
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "UGameDebugBlueprintLibrary.generated.h"

/**
 * @brief 面向蓝图的 GameDebug 调试诊断函数库
 */
UCLASS()
class GAMEDEBUG_API UGameDebugBlueprintLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:
    /**
     * @brief 蓝图专用：向屏幕输出基础调试消息
     * @param Message 消息文本
     * @param Color 字体颜色
     * @param Duration 屏幕驻留时长（秒）
     * @param Key 覆盖槽位 Key（-1 为追加）
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Print")
    static void PrintDebugMessage(
        const FString& Message,
        FColor Color = FColor::Cyan,
        float Duration = 2.0f,
        int64 Key = -1
    );

    /**
     * @brief 蓝图专用：基于通道名称输出屏幕调试消息（同通道原地刷新防刷屏）
     * @param Channel 通道唯一名称
     * @param Message 消息文本
     * @param Color 字体颜色
     * @param Duration 屏幕驻留时长（秒）
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Print")
    static void PrintChannelMessage(
        FName Channel,
        const FString& Message,
        FColor Color = FColor::Yellow,
        float Duration = 2.0f
    );

    /**
     * @brief 蓝图专用：向屏幕输出高亮警告消息（橙色）
     * @param Message 警告文本
     * @param Duration 屏幕驻留时长（秒）
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Print")
    static void PrintWarning(const FString& Message, float Duration = 5.0f);

    /**
     * @brief 蓝图专用：向屏幕输出高亮错误消息（红色）
     * @param Message 错误文本
     * @param Duration 屏幕驻留时长（秒）
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Print")
    static void PrintError(const FString& Message, float Duration = 5.0f);

    /**
     * @brief 蓝图专用：绘制调试线段
     * @param WorldContextObject 世界上下文
     * @param LineStart 起点坐标
     * @param LineEnd 终点坐标
     * @param Color 线段颜色
     * @param Duration 持续秒数（0 为单帧）
     * @param Thickness 线条粗细
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugLine(
        const UObject* WorldContextObject,
        FVector LineStart,
        FVector LineEnd,
        FColor Color = FColor::Green,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 蓝图专用：绘制调试球体
     * @param WorldContextObject 世界上下文
     * @param Center 球心坐标
     * @param Radius 球体半径
     * @param Color 线框颜色
     * @param Segments 细分段数
     * @param Duration 持续秒数（0 为单帧）
     * @param Thickness 线条粗细
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugSphere(
        const UObject* WorldContextObject,
        FVector Center,
        float Radius = 50.0f,
        FColor Color = FColor::Cyan,
        int32 Segments = 16,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 蓝图专用：绘制调试包围盒
     * @param WorldContextObject 世界上下文
     * @param Center 中心坐标
     * @param Extent 半尺寸长宽高
     * @param Rotation 盒体朝向
     * @param Color 线框颜色
     * @param Duration 持续秒数（0 为单帧）
     * @param Thickness 线条粗细
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugBox(
        const UObject* WorldContextObject,
        FVector Center,
        FVector Extent = FVector(50.0, 50.0, 50.0),
        FRotator Rotation = FRotator::ZeroRotator,
        FColor Color = FColor::Yellow,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 蓝图专用：绘制调试胶囊体
     * @param WorldContextObject 世界上下文
     * @param Center 中心坐标
     * @param HalfHeight 胶囊体半高
     * @param Radius 胶囊体半径
     * @param Rotation 胶囊体旋转朝向
     * @param Color 线框颜色
     * @param Duration 持续秒数（0 为单帧）
     * @param Thickness 线条粗细
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugCapsule(
        const UObject* WorldContextObject,
        FVector Center,
        float HalfHeight = 88.0f,
        float Radius = 34.0f,
        FRotator Rotation = FRotator::ZeroRotator,
        FColor Color = FColor::Green,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 蓝图专用：绘制调试圆柱体
     * @param WorldContextObject 世界上下文
     * @param Start 底面中心
     * @param End 顶面中心
     * @param Radius 底面半径
     * @param Segments 细分段数
     * @param Color 线框颜色
     * @param Duration 持续秒数（0 为单帧）
     * @param Thickness 线条粗细
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugCylinder(
        const UObject* WorldContextObject,
        FVector Start,
        FVector End,
        float Radius = 50.0f,
        int32 Segments = 16,
        FColor Color = FColor::Yellow,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 蓝图专用：绘制调试视野/攻击扇形圆锥
     * @param WorldContextObject 世界上下文
     * @param Origin 锥顶点坐标
     * @param Direction 主轴朝向
     * @param Length 锥体轴向长度
     * @param AngleWidthDeg 水平张角角度（度）
     * @param AngleHeightDeg 垂直张角角度（度）
     * @param NumSides 侧面细分线数量
     * @param Color 线框颜色
     * @param Duration 持续秒数（0 为单帧）
     * @param Thickness 线条粗细
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugCone(
        const UObject* WorldContextObject,
        FVector Origin,
        FVector Direction,
        float Length = 200.0f,
        float AngleWidthDeg = 45.0f,
        float AngleHeightDeg = 45.0f,
        int32 NumSides = 16,
        FColor Color = FColor::Yellow,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 蓝图专用：绘制调试方向箭头
     * @param WorldContextObject 世界上下文
     * @param LineStart 箭头起点
     * @param LineEnd 箭头终点
     * @param ArrowSize 箭头头部尺寸
     * @param Color 线框颜色
     * @param Duration 持续秒数（0 为单帧）
     * @param Thickness 线条粗细
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugArrow(
        const UObject* WorldContextObject,
        FVector LineStart,
        FVector LineEnd,
        float ArrowSize = 20.0f,
        FColor Color = FColor::Red,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 蓝图专用：绘制局部坐标轴三向线（红绿蓝）
     * @param WorldContextObject 世界上下文
     * @param AxisLoc 坐标系原点
     * @param AxisRot 坐标系朝向
     * @param Scale 坐标轴线段长度
     * @param Duration 持续秒数（0 为单帧）
     * @param Thickness 线条粗细
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugCoordinateSystem(
        const UObject* WorldContextObject,
        FVector AxisLoc,
        FRotator AxisRot,
        float Scale = 50.0f,
        float Duration = 0.0f,
        float Thickness = 1.0f
    );

    /**
     * @brief 蓝图专用：在 3D 空间世界坐标绘制悬浮调试文本
     * @param WorldContextObject 世界上下文
     * @param TextLocation 空间位置
     * @param Text 呈现文本
     * @param TextColor 文本颜色
     * @param Duration 持续秒数（0 为单帧）
     * @param bDrawShadow 是否带有阴影
     * @param FontScale 字体缩放比例
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
    static void DrawDebugString(
        const UObject* WorldContextObject,
        FVector TextLocation,
        const FString& Text,
        FColor TextColor = FColor::White,
        float Duration = 0.0f,
        bool bDrawShadow = false,
        float FontScale = 1.0f
    );

    /**
     * @brief 蓝图专用：设置全局时间膨胀缩放倍率（慢动作或快进）
     * @param WorldContextObject 世界上下文
     * @param TimeDilation 时间缩放倍率 (0.001 ~ 20.0)
     * @return 设置成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
    static bool SetGlobalTimeDilation(const UObject* WorldContextObject, float TimeDilation);

    /**
     * @brief 蓝图专用：获取当前全局时间膨胀缩放倍率
     * @param WorldContextObject 世界上下文
     * @return 当前时间缩放倍率
     */
    UFUNCTION(BlueprintPure, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
    static float GetGlobalTimeDilation(const UObject* WorldContextObject);

    /**
     * @brief 蓝图专用：判定当前游戏世界时钟是否处于暂停/冻结状态
     * @param WorldContextObject 世界上下文
     * @return 已暂停返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
    static bool IsGamePaused(const UObject* WorldContextObject);

    /**
     * @brief 蓝图专用：暂停游戏世界时钟
     * @param WorldContextObject 世界上下文
     * @return 执行成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
    static bool PauseGameTime(const UObject* WorldContextObject);

    /**
     * @brief 蓝图专用：恢复游戏世界时钟至暂停前速度（如未曾暂停则恢复至 1.0x）
     * @param WorldContextObject 世界上下文
     * @return 执行成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
    static bool ResumeGameTime(const UObject* WorldContextObject);

    /**
     * @brief 蓝图专用：重置游戏世界时钟为 1.0 标准正常速度
     * @param WorldContextObject 世界上下文
     * @return 执行成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
    static bool ResetGameTime(const UObject* WorldContextObject);

    /**
     * @brief 蓝图专用：单步逐帧推进世界时钟（运行指定帧后自动重新暂停）
     * @param WorldContextObject 世界上下文
     * @param Frames 步进帧数（默认 1 帧）
     * @return 执行成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
    static bool StepGameFrames(const UObject* WorldContextObject, int32 Frames = 1);
};
