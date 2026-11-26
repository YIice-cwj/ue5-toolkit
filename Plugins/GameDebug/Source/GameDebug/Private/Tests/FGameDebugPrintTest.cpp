/**
 * @file FGameDebugPrintTest.cpp
 * @brief FGameDebugPrint 屏幕与通道调试输出真测试（槽位正交隔离、覆盖、钳制、宏展开与多线程并发打印）
 */

#include "Print/FGameDebugPrint.h"

#include "Core/GameDebugScreenKeys.h"
#include "Tests/GameDebugTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugPrintChannelTest,
    "Game.Debug.Print.ChannelAndMacros",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 64 位域盐槽位键确定性、与 WatchHub 正交隔离、屏幕消息真实写入/覆写/追加新行、宏展开及多线程并发打印安全
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameDebugPrintChannelTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestEqual(TEXT("NAME_None 通道 Key 为 InvalidKey"), FGameDebugPrint::GetChannelKey(NAME_None), FGameDebugPrint::InvalidKey);

    const FName ChannelCombat(TEXT("Combat"));
    const FName ChannelInventory(TEXT("Inventory"));
    const uint64 CombatKey1 = FGameDebugPrint::GetChannelKey(ChannelCombat);
    const uint64 CombatKey2 = FGameDebugPrint::GetChannelKey(ChannelCombat);
    const uint64 InventoryKey = FGameDebugPrint::GetChannelKey(ChannelInventory);

    TestEqual(TEXT("相同通道名称计算所得 Key 完全一致"), CombatKey1, CombatKey2);
    TestNotEqual(TEXT("有效通道 Key 永不等于 InvalidKey"), CombatKey1, FGameDebugPrint::InvalidKey);
    TestNotEqual(TEXT("不同通道名称 Key 互不冲突"), CombatKey1, InventoryKey);

    const uint64 WatchCombatKey = Game::Debug::Private::MakeWatchHubCategoryKey(ChannelCombat);
    TestNotEqual(TEXT("同名通道与 WatchHub 分类 64 位键严格正交"), CombatKey1, WatchCombatKey);
    TestNotEqual(
        TEXT("同名通道与 WatchHub 分类截断至 UEngine int32 键后仍严格正交"),
        static_cast<int32>(CombatKey1),
        static_cast<int32>(WatchCombatKey)
    );

    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    TestNotNull(TEXT("自动化测试环境 GEngine 有效"), GEngine);
    if (GEngine == nullptr) {
        return false;
    }

    Game::Debug::Test::FScopedPrintCapture PrintCapture;
    Game::Debug::Test::FCapturedPrintRecord Captured;

    FGameDebugPrint::PrintChannel(ChannelCombat, TEXT("First Hit 100"), FColor::Yellow, 2.5f);
    TestTrue(TEXT("UEngine 公开查询确认 Combat 槽位已注册"), Game::Debug::Test::HasKeyedScreenMessage(CombatKey1));
    TestTrue(TEXT("捕获到 Combat 通道首次打印"), PrintCapture.FindLastByKey(CombatKey1, Captured));
    TestEqual(TEXT("通道消息自动拼接 [Channel] 前缀"), Captured.Message, FString(TEXT("[Combat] First Hit 100")));
    TestEqual(TEXT("通道消息颜色匹配黄色"), Captured.Color, FColor::Yellow);
    TestEqual(TEXT("通道消息驻留时长为 2.5f"), Captured.Duration, 2.5f);

    FGameDebugPrint::PrintChannel(ChannelCombat, TEXT("Second Hit 250"), FColor::Green, 4.0f);
    TestTrue(TEXT("原地覆盖后引擎槽位依然存在"), Game::Debug::Test::HasKeyedScreenMessage(CombatKey1));
    TestTrue(TEXT("捕获到 Combat 通道覆写打印"), PrintCapture.FindLastByKey(CombatKey1, Captured));
    TestEqual(TEXT("同通道正文已更新"), Captured.Message, FString(TEXT("[Combat] Second Hit 250")));
    TestEqual(TEXT("同通道颜色已更新"), Captured.Color, FColor::Green);

    FGameDebugPrint::PrintChannel(ChannelInventory, TEXT("Picked Potion"), FColor::Cyan, -5.0f);
    TestTrue(TEXT("Inventory 通道独立槽位在引擎内注册成功"), Game::Debug::Test::HasKeyedScreenMessage(InventoryKey));
    TestTrue(TEXT("捕获到 Inventory 通道打印"), PrintCapture.FindLastByKey(InventoryKey, Captured));
    TestEqual(TEXT("负数 Duration 被安全钳制为 0.0f"), Captured.Duration, 0.0f);

    const int32 BaseInvalidKeyCount = PrintCapture.CountByKey(FGameDebugPrint::InvalidKey);
    FGameDebugPrint::Print(TEXT("Append Line"), FColor::Cyan, 1.5f);
    FGameDebugPrint::PrintWarning(TEXT("Low Stamina"), 5.0f);
    FGameDebugPrint::PrintError(TEXT("Net Error"), 5.0f);
    TestEqual(TEXT("3 次追加打印均使用 InvalidKey"), PrintCapture.CountByKey(FGameDebugPrint::InvalidKey), BaseInvalidKeyCount + 3);
    const TArray<Game::Debug::Test::FCapturedPrintRecord> Records = PrintCapture.GetRecordsSnapshot();
    TestTrue(TEXT("捕获记录数足够校验 Warn/Error 颜色"), Records.Num() >= 3);
    if (Records.Num() >= 3) {
        TestEqual(TEXT("PrintWarning 颜色为橙色"), Records[Records.Num() - 2].Color, FColor::Orange);
        TestEqual(TEXT("PrintError 颜色为红色"), Records[Records.Num() - 1].Color, FColor::Red);
    }

    const int32 PreMacroInvalidCount = PrintCapture.CountByKey(FGameDebugPrint::InvalidKey);
    const bool bRunMacroBranch = (GEngine != nullptr);
    if (bRunMacroBranch)
        GAME_DEBUG_PRINT("MacroPrint %d", 42);
    else
        TestTrue(TEXT("不可达分支"), false);
    GAME_DEBUG_PRINT_COLOR(FColor::Blue, 3.0f, "MacroColor %s", TEXT("Blue"));
    GAME_DEBUG_PRINT_WARN("MacroWarn %d", 7);
    GAME_DEBUG_PRINT_ERROR("MacroError %s", TEXT("Fatal"));
    GAME_DEBUG_PRINT_CHANNEL(ChannelCombat, "MacroChannel %s", TEXT("Crit"));

    TestEqual(TEXT("4 条追加型打印宏共派发 4 条 InvalidKey 消息"), PrintCapture.CountByKey(FGameDebugPrint::InvalidKey), PreMacroInvalidCount + 4);
    TestTrue(TEXT("GAME_DEBUG_PRINT_CHANNEL 刷新目标通道"), PrintCapture.FindLastByKey(CombatKey1, Captured));
    TestEqual(TEXT("通道宏格式化正文正确"), Captured.Message, FString(TEXT("[Combat] MacroChannel Crit")));

    PrintCapture.Reset();
    constexpr int32 ConcurrentPrints = 64;
    ParallelFor(ConcurrentPrints, [&](int32 WorkerIdx) {
        FGameDebugPrint::PrintChannel(
            FName(*FString::Printf(TEXT("MTPrint_%d"), WorkerIdx & 7)),
            FString::Printf(TEXT("Worker %d"), WorkerIdx),
            FColor::White,
            1.0f
        );
    });
    TestEqual(TEXT("多线程并发打印全部线程安全送达"), PrintCapture.Num(), ConcurrentPrints);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
