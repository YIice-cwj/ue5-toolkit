/**
 * @file NetHeartbeatKeeperTest.cpp
 * @brief 长连接周期心跳保活、RTT 测算与对端失步超时侦测器（FNetHeartbeatKeeper）深度单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Policy/NetHeartbeatKeeper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetHeartbeatKeeperTest,
    "Game.Net.Service.HeartbeatKeeper",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证周期 Ping 触发、对端数据静默计时清零、Pong RTT 精确换算与超时断线判定
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetHeartbeatKeeperTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetHeartbeatConfig Config;
    Config.bEnabled = true;
    Config.IntervalSeconds = 5.0f;
    Config.TimeoutSeconds = 12.0f;
    Config.PingMessageId = 10;
    Config.PongMessageId = 11;

    FNetHeartbeatKeeper Keeper(Config);
    TestEqual(TEXT("零时间步进返回 None"), Keeper.Tick(0.0f), ENetHeartbeatAction::None);
    TestEqual(TEXT("推进 4.9s 未达心跳周期返回 None"), Keeper.Tick(4.9f), ENetHeartbeatAction::None);
    TestEqual(TEXT("跨过 5.0s 触发 SendPing"), Keeper.Tick(0.2f), ENetHeartbeatAction::SendPing);

    Keeper.RecordPingSent(77, 100.0);
    Keeper.NotifyInboundPacket(999, 0, 100.02);
    TestEqual(TEXT("非 Pong 业务报文同样重置静默秒数为 0"), Keeper.GetSecondsSinceLastInbound(), 0.0f);
    TestEqual(TEXT("非 Pong 报文不误写 RTT"), Keeper.GetLastRttMilliseconds(), 0.0f);

    Keeper.NotifyInboundPacket(11, 77, 100.045);
    TestTrue(TEXT("匹配序列号的 Pong 精确计算出 45ms RTT"), FMath::IsNearlyEqual(Keeper.GetLastRttMilliseconds(), 45.0f, 0.1f));

    TestEqual(TEXT("静默 6.0s 触发一次 Ping"), Keeper.Tick(6.0f), ENetHeartbeatAction::SendPing);
    TestEqual(TEXT("继续静默 6.1s 累计超过 12.0s 触发 TimeoutDisconnect"), Keeper.Tick(6.1f), ENetHeartbeatAction::TimeoutDisconnect);

    Keeper.Reset();
    TestEqual(TEXT("Reset 后 RTT 清零"), Keeper.GetLastRttMilliseconds(), 0.0f);
    TestEqual(TEXT("Reset 后静默计时清零"), Keeper.GetSecondsSinceLastInbound(), 0.0f);

    FNetHeartbeatConfig DisabledCfg = Config;
    DisabledCfg.bEnabled = false;
    Keeper.SetConfig(DisabledCfg);
    TestFalse(TEXT("心跳配置更新为禁用"), Keeper.GetConfig().bEnabled);
    TestEqual(TEXT("禁用状态下长时步进始终返回 None"), Keeper.Tick(100.0f), ENetHeartbeatAction::None);

    return true;
}

#endif

