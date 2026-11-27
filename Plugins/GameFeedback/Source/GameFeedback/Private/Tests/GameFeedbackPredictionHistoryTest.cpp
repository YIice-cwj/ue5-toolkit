/**
 * @file GameFeedbackPredictionHistoryTest.cpp
 * @brief 本地预测历史队列匹配、有界上限淘汰与失效实体清理自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "GameFramework/Actor.h"
#include "Tracker/GameFeedbackPredictionHistory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackPredictionHistoryTest,
    "Game.Feedback.PredictionHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证预测事件按非零 ID 与双实体精确匹配、乱序消费、0.5s 过期、有界容量淘汰与失效弱引用清理
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackPredictionHistoryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* Attacker = NewObject<AActor>(GetTransientPackage());
    AActor* OtherAttacker = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    FGameFeedbackPredictionHistory History;
    History.Add(7, Attacker, Target, 1.0);
    TestFalse(TEXT("同实体不同 ID 不得误消费"), History.ConsumeMatch(8, Attacker, Target, 1.1));
    TestFalse(TEXT("实体条件仍须匹配"), History.ConsumeMatch(7, OtherAttacker, Target, 1.1));
    TestTrue(TEXT("有效窗口内应消费相同事件 ID"), History.ConsumeMatch(7, Attacker, Target, 1.1));
    TestFalse(TEXT("重复回执不得二次消费"), History.ConsumeMatch(7, Attacker, Target, 1.2));

    History.Add(7, Attacker, Target, 2.0);
    History.Add(8, Attacker, Target, 2.0);
    History.Add(9, Attacker, Target, 2.0);
    TestTrue(TEXT("乱序回执 ID 9 应独立消费"), History.ConsumeMatch(9, Attacker, Target, 2.1));
    TestTrue(TEXT("ID 7 回执丢失不影响 ID 8"), History.ConsumeMatch(8, Attacker, Target, 2.1));
    TestFalse(TEXT("未预测事件不得被旧记录误抑制"), History.ConsumeMatch(10, Attacker, Target, 2.1));
    TestTrue(TEXT("迟到的 ID 7 仍可准确消费"), History.ConsumeMatch(7, Attacker, Target, 2.2));

    History.Add(0, Attacker, Target, 3.0);
    TestFalse(TEXT("ID 0 永不记录或消费预测"), History.ConsumeMatch(0, Attacker, Target, 3.1));

    History.Add(11, Attacker, Target, 4.0);
    TestFalse(TEXT("超出 0.5 秒窗口的指纹应被清理"), History.ConsumeMatch(11, Attacker, Target, 4.6));

    const int32 OverflowCount = FGameFeedbackPredictionHistory::MaxEntries + 16;
    for (int32 Index = 1; Index <= OverflowCount; ++Index) {
        History.Add(static_cast<uint16>(Index), Attacker, Target, 10.0 + Index * 0.001);
    }
    TestEqual(TEXT("高频突发事件不得突破最大有界容量"), History.GetEntryCount(), FGameFeedbackPredictionHistory::MaxEntries);
    TestFalse(TEXT("最早的记录应被容量淘汰"), History.ConsumeMatch(1, Attacker, Target, 10.2));
    TestTrue(TEXT("最新的记录应保留并成功消费"), History.ConsumeMatch(static_cast<uint16>(OverflowCount), Attacker, Target, 10.2));

    AActor* StaleTarget = NewObject<AActor>(GetTransientPackage());
    History.Prune(20.0);
    History.Add(500, Attacker, StaleTarget, 20.0);
    TestEqual(TEXT("写入失效目标前应持有 1 条记录"), History.GetEntryCount(), 1);
    StaleTarget->MarkAsGarbage();
    History.Prune(20.05);
    TestEqual(TEXT("Prune 应自动剔除指向已销毁实体的过期弱引用"), History.GetEntryCount(), 0);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
