/**
 * @file FGameRandomManagerPureTest.cpp
 * @brief 验证 FGameRandomManager 纯 C++ 算法核心脱离 UWorld 在独立环境下的通道隔离与二进制归档能力
 */

#include "Random/FGameRandomManager.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRandomManagerPureTest,
    "Game.Core.Random.RandomManager.PureCppExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameRandomManager 脱离 UWorld 独立流隔离、确定性派生与归档恢复
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameRandomManagerPureTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRandomManager Manager;
    TestEqual(TEXT("初始通道数为 0"), Manager.GetStreamCount(), 0);

    const FName CombatChannel(TEXT("Combat"));
    const FName LootChannel(TEXT("Loot"));

    FGameRandomStream& CombatStream = Manager.GetOrCreateStream(CombatChannel);
    TestEqual(TEXT("创建首个通道后总数为 1"), Manager.GetStreamCount(), 1);
    TestTrue(TEXT("存在 Combat 通道"), Manager.HasStream(CombatChannel));

    FGameRandomStream& LootStream = Manager.InitializeStream(LootChannel, 12345678ULL);
    TestEqual(TEXT("初始化第二个通道后总数为 2"), Manager.GetStreamCount(), 2);
    TestTrue(TEXT("存在 Loot 通道"), Manager.HasStream(LootChannel));

    const int32 Val1 = CombatStream.RandRange(1, 100);
    const int32 Val2 = LootStream.RandRange(1, 100);
    TestTrue(TEXT("双通道独立抽样有效"), Val1 >= 1 && Val1 <= 100 && Val2 >= 1 && Val2 <= 100);

    FGameRandomStream* OriginalCombatAddress = &CombatStream;
    for (int32 Index = 0; Index < 128; ++Index) {
        Manager.GetOrCreateStream(FName(*FString::Printf(TEXT("DynamicChannel_%d"), Index)));
    }
    TestEqual(
        TEXT("TMap 大规模扩容后之前获取的 FGameRandomStream 引用物理地址 100% 稳定"),
        &Manager.GetOrCreateStream(CombatChannel),
        OriginalCombatAddress
    );
    for (int32 Index = 0; Index < 128; ++Index) {
        Manager.RemoveStream(FName(*FString::Printf(TEXT("DynamicChannel_%d"), Index)));
    }

    FGameRandomManager CaseManagerA;
    FGameRandomManager CaseManagerB;
    TestEqual(
        TEXT("通道名不同大小写派生相同的确定性默认种子与初始状态"),
        CaseManagerA.GetOrCreateStream(FName(TEXT("CriticalStrike"))).GetState(),
        CaseManagerB.GetOrCreateStream(FName(TEXT("criticalstrike"))).GetState()
    );

    FGameRandomManager CopiedManager = Manager;
    const uint64 StateBeforeMutateCopy = LootStream.GetState();
    CopiedManager.GetOrCreateStream(LootChannel).RandRange(1, 1000);
    TestEqual(TEXT("拷贝管理器具有深拷贝独立性，修改副本不影响原流状态"), LootStream.GetState(), StateBeforeMutateCopy);

    FBufferArchive Archive;
    Manager.ExportStreams(Archive);
    TestTrue(TEXT("导出二进制归档字节数大于 0"), Archive.Num() > 0);

    FGameRandomManager RestoredManager;
    FMemoryReader Reader(Archive);
    TestTrue(TEXT("导入二进制归档成功"), RestoredManager.ImportStreams(Reader));
    TestEqual(TEXT("恢复后通道数一致"), RestoredManager.GetStreamCount(), 2);
    TestTrue(TEXT("恢复后存在 Combat"), RestoredManager.HasStream(CombatChannel));
    TestTrue(TEXT("恢复后存在 Loot"), RestoredManager.HasStream(LootChannel));

    FGameRandomStream& RestoredLoot = RestoredManager.GetOrCreateStream(LootChannel);
    TestEqual(TEXT("两管理器流状态严格一致"), RestoredLoot.GetState(), LootStream.GetState());

    TestTrue(TEXT("移除 Combat 通道成功"), Manager.RemoveStream(CombatChannel));
    TestEqual(TEXT("移除后通道数减少"), Manager.GetStreamCount(), 1);

    Manager.ClearAllStreams();
    TestEqual(TEXT("全部清理后通道数为 0"), Manager.GetStreamCount(), 0);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
