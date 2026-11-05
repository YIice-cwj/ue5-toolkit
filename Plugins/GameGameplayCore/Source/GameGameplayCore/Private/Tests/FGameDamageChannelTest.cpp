/**
 * @file FGameDamageChannelTest.cpp
 * @brief FGameDamageChannel 通道运算自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Damage/FGameDamageChannel.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageChannelTest,
    "Game.GameplayCore.Damage.ChannelOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameDamageChannel 稀疏通道的增删查改、精确匹配与 GameplayTag 层级继承汇总
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameDamageChannelTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag PhysTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Op.Physical")));
    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Op.Physical.Slash")));
    const FGameplayTag BluntTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Op.Physical.Blunt")));
    const FGameplayTag FireTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Op.Elemental.Fire")));

    TArray<FGameDamageChannel> Channels;

    TestNull(TEXT("空数组查找返回 nullptr"), FGameDamageChannel::FindExact(Channels, SlashTag));
    TestEqual(TEXT("空数组获取数值返回默认值"), FGameDamageChannel::GetExact(Channels, SlashTag, 10.0f), 10.0f);
    TestEqual(TEXT("空数组求和返回 0"), FGameDamageChannel::GetSum(Channels, PhysTag), 0.0f);

    FGameDamageChannel::SetExact(Channels, SlashTag, 50.0f);
    TestNotNull(TEXT("设置后能查找到对应通道"), FGameDamageChannel::FindExact(Channels, SlashTag));
    TestEqual(TEXT("精确获取数值正确"), FGameDamageChannel::GetExact(Channels, SlashTag), 50.0f);

    FGameDamageChannel::SetExact(Channels, SlashTag, 60.0f);
    TestEqual(TEXT("重复设置覆盖原数值"), FGameDamageChannel::GetExact(Channels, SlashTag), 60.0f);
    TestEqual(TEXT("覆盖未增加多余条目"), Channels.Num(), 1);

    FGameDamageChannel::AddExact(Channels, BluntTag, 40.0f);
    TestEqual(TEXT("AddExact 新增条目数值正确"), FGameDamageChannel::GetExact(Channels, BluntTag), 40.0f);
    TestEqual(TEXT("新增后条目数量为 2"), Channels.Num(), 2);

    FGameDamageChannel::AddExact(Channels, BluntTag, 15.0f);
    TestEqual(TEXT("AddExact 累加既有条目数值正确"), FGameDamageChannel::GetExact(Channels, BluntTag), 55.0f);

    FGameDamageChannel::SetExact(Channels, FireTag, 30.0f);
    TestEqual(TEXT("父标签匹配子标签层级求和"), FGameDamageChannel::GetSum(Channels, PhysTag), 115.0f);
    TestEqual(TEXT("不同分支标签求和独立"), FGameDamageChannel::GetSum(Channels, FireTag), 30.0f);

    FGameDamageChannel* MutableItem = FGameDamageChannel::FindExactMutable(Channels, SlashTag);
    TestNotNull(TEXT("可变查找返回有效指针"), MutableItem);
    if (MutableItem != nullptr) {
        MutableItem->Magnitude = 70.0f;
    }
    TestEqual(TEXT("通过可变指针修改成功生效"), FGameDamageChannel::GetExact(Channels, SlashTag), 70.0f);

    TestTrue(TEXT("精确移除 Slash 通道成功"), FGameDamageChannel::RemoveExact(Channels, SlashTag));
    TestNull(TEXT("移除后查找返回 nullptr"), FGameDamageChannel::FindExact(Channels, SlashTag));
    TestFalse(TEXT("重复移除不存在通道返回 false"), FGameDamageChannel::RemoveExact(Channels, SlashTag));
    TestEqual(TEXT("移除后剩余通道数正确"), Channels.Num(), 2);

    return true;
}

#endif
