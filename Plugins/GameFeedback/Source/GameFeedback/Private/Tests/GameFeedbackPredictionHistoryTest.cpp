/**
 * @file GameFeedbackPredictionHistoryTest.cpp
 * @brief 本地预测历史队列匹配与去重自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "GameFramework/Actor.h"
#include "System/GameFeedbackPredictionHistory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackPredictionHistoryTest,
    "Game.Feedback.PredictionHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证预测事件仅按非零 ID 与双实体匹配，并覆盖乱序、丢失、重复与过期
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
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

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
