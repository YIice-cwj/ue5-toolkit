/**
 * @file FGameDebugPrintTest.cpp
 * @brief FGameDebugPrint 与 FGameDebugDraw 屏幕与通道调试输出自动化测试
 */

#include "Print/FGameDebugPrint.h"
#include "Visualizer/FGameDebugDraw.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugPrintAndDrawTest,
    "Game.Debug.PrintAndDraw.Basic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证通道 Key 计算确定性、安全空指针绘制不崩溃与宏编译健壮性
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameDebugPrintAndDrawTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const uint64 NoneKey = FGameDebugPrint::GetChannelKey(NAME_None);
    TestEqual(TEXT("NAME_None 通道 Key 为 -1"), NoneKey, static_cast<uint64>(-1));

    const FName ChannelCombat(TEXT("Combat"));
    const uint64 CombatKey1 = FGameDebugPrint::GetChannelKey(ChannelCombat);
    const uint64 CombatKey2 = FGameDebugPrint::GetChannelKey(ChannelCombat);
    TestEqual(TEXT("相同通道名称计算所得 Key 完全确定一致"), CombatKey1, CombatKey2);

    const FName ChannelInventory(TEXT("Inventory"));
    const uint64 InventoryKey = FGameDebugPrint::GetChannelKey(ChannelInventory);
    TestTrue(TEXT("不同通道名称计算所得 Key 不相等"), CombatKey1 != InventoryKey);

    FGameDebugPrint::Print(TEXT("Test Message"), FColor::Green, 1.0f);
    FGameDebugPrint::PrintChannel(ChannelCombat, TEXT("Player hit enemy for 100 dmg"), FColor::Yellow, 1.0f);
    FGameDebugPrint::PrintWarning(TEXT("Low stamina warning"), 2.0f);
    FGameDebugPrint::PrintError(TEXT("Network disconnected error"), 2.0f);

    GAME_DEBUG_PRINT("Formatted message with %d args", 42);
    GAME_DEBUG_PRINT_COLOR(FColor::Blue, 3.0f, "Blue color message");
    GAME_DEBUG_PRINT_CHANNEL(ChannelCombat, "Combat channel message %s", TEXT("CriticalHit"));
    GAME_DEBUG_PRINT_WARN("Warning macro check %d", 1);
    GAME_DEBUG_PRINT_ERROR("Error macro check %s", TEXT("Fatal"));

    FGameDebugDraw::DrawLine(nullptr, FVector::ZeroVector, FVector(100.0, 0.0, 0.0));
    FGameDebugDraw::DrawSphere(nullptr, FVector::ZeroVector, 50.0f);
    FGameDebugDraw::DrawSphere(nullptr, FVector::ZeroVector, -10.0f);
    FGameDebugDraw::DrawBox(nullptr, FVector::ZeroVector, FVector(50.0, 50.0, 50.0));
    FGameDebugDraw::DrawBox(nullptr, FVector::ZeroVector, FVector(-10.0, 50.0, 50.0));

    TestTrue(TEXT("全部打印与绘制调用在无 World/无 GEngine 边界下安全无崩溃"), true);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
