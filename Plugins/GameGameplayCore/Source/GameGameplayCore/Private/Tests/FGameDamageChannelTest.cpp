/**
 * @file FGameDamageChannelTest.cpp
 * @brief FGameDamageChannel 通道运算自动化测试
 */

#include "CoreMinimal.h"
#include "Damage/FGameDamageChannel.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageChannelTest,
    "Game.GameplayCore.Damage.ChannelOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameDamageChannel 稀疏通道的增删查改、精确匹配、层级汇总、总和与主导通道判定
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
    TestEqual(TEXT("空数组总和返回 0"), FGameDamageChannel::GetTotalMagnitude(Channels), 0.0f);
    TestFalse(TEXT("空数组主导通道无效"), FGameDamageChannel::GetDominantChannel(Channels).IsValid());

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
    TestEqual(TEXT("全部通道数值总和正确"), FGameDamageChannel::GetTotalMagnitude(Channels), 145.0f);
    TestEqual(TEXT("主导通道命中数值最大者"), FGameDamageChannel::GetDominantChannel(Channels), SlashTag);

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
    TestEqual(TEXT("移除后主导通道更新为 Blunt"), FGameDamageChannel::GetDominantChannel(Channels), BluntTag);

    const FGameplayTag EmptyTag;
    TestNull(TEXT("无效 Tag 查找返回 nullptr"), FGameDamageChannel::FindExact(Channels, EmptyTag));
    TestNull(TEXT("无效 Tag 可变查找返回 nullptr"), FGameDamageChannel::FindExactMutable(Channels, EmptyTag));
    TestEqual(TEXT("无效 Tag 获取数值返回默认值"), FGameDamageChannel::GetExact(Channels, EmptyTag, 99.0f), 99.0f);
    TestEqual(TEXT("无效 Tag 求和返回 0"), FGameDamageChannel::GetSum(Channels, EmptyTag), 0.0f);
    FGameDamageChannel::SetExact(Channels, EmptyTag, 100.0f);
    TestEqual(TEXT("设置无效 Tag 不改变数组大小"), Channels.Num(), 2);
    FGameDamageChannel::AddExact(Channels, EmptyTag, 100.0f);
    TestEqual(TEXT("累加无效 Tag 不改变数组大小"), Channels.Num(), 2);
    TestFalse(TEXT("移除无效 Tag 返回 false"), FGameDamageChannel::RemoveExact(Channels, EmptyTag));

    TArray<FGameDamageChannel> NaNLeadingChannels;
    NaNLeadingChannels.Emplace(FireTag, std::numeric_limits<float>::quiet_NaN());
    TestFalse(TEXT("全部为 NaN 时主导通道返回无效标签"), FGameDamageChannel::GetDominantChannel(NaNLeadingChannels).IsValid());
    NaNLeadingChannels.Emplace(BluntTag, 25.0f);
    NaNLeadingChannels.Emplace(SlashTag, 40.0f);
    TestEqual(TEXT("首位 NaN 通道不掩盖后续有限主导通道"), FGameDamageChannel::GetDominantChannel(NaNLeadingChannels), SlashTag);

    return true;
}

#endif
