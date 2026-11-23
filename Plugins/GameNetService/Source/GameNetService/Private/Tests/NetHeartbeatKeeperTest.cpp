/**
 * @file NetHeartbeatKeeperTest.cpp
 * @brief 长连接周期心跳保活、单次与滑动平均 RTT 测算与对端失步超时侦测器（FNetHeartbeatKeeper）深度单元测试
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
 * @brief 验证配置合法性校验、周期 Ping 触发、错序 Pong 忽略、单次/平滑 RTT 换算与超时断线判定
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetHeartbeatKeeperTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetHeartbeatConfig DefaultCfg;
    TestTrue(TEXT("默认心跳配置合法"), DefaultCfg.IsValid());
    TestEqual(TEXT("默认 PingMessageId 与 NET_MSG_HEARTBEAT_PING 一致"), DefaultCfg.PingMessageId, NET_MSG_HEARTBEAT_PING);
    TestEqual(TEXT("默认 PongMessageId 与 NET_MSG_HEARTBEAT_PONG 一致"), DefaultCfg.PongMessageId, NET_MSG_HEARTBEAT_PONG);

    FNetHeartbeatConfig BadCfg = DefaultCfg;
    BadCfg.TimeoutSeconds = BadCfg.IntervalSeconds;
    TestFalse(TEXT("Timeout <= Interval 时 IsValid 为 false"), BadCfg.IsValid());

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

    Keeper.NotifyInboundPacket(11, 999, 100.03);
    TestEqual(TEXT("序列号不匹配的错序 Pong 不更新 RTT"), Keeper.GetLastRttMilliseconds(), 0.0f);

    Keeper.NotifyInboundPacket(11, 77, 100.045);
    TestTrue(TEXT("匹配序列号的 Pong 精确计算出 45ms RTT"), FMath::IsNearlyEqual(Keeper.GetLastRttMilliseconds(), 45.0f, 0.1f));
    TestTrue(TEXT("单样本平滑 RTT 等于 45ms"), FMath::IsNearlyEqual(Keeper.GetAverageRttMilliseconds(), 45.0f, 0.1f));

    Keeper.RecordPingSent(78, 200.0);
    Keeper.NotifyInboundPacket(11, 0, 200.025);
    TestTrue(TEXT("缺省序列号 0 的 Pong 同样安全兼容计算出 25ms RTT"), FMath::IsNearlyEqual(Keeper.GetLastRttMilliseconds(), 25.0f, 0.1f));
    TestTrue(TEXT("两样本平滑平均 RTT 精确为 35ms"), FMath::IsNearlyEqual(Keeper.GetAverageRttMilliseconds(), 35.0f, 0.1f));

    TestEqual(TEXT("静默 6.0s 触发一次 Ping"), Keeper.Tick(6.0f), ENetHeartbeatAction::SendPing);
    TestEqual(TEXT("继续静默 6.1s 累计超过 12.0s 触发 TimeoutDisconnect"), Keeper.Tick(6.1f), ENetHeartbeatAction::TimeoutDisconnect);

    Keeper.Reset();
    TestEqual(TEXT("Reset 后最新 RTT 清零"), Keeper.GetLastRttMilliseconds(), 0.0f);
    TestEqual(TEXT("Reset 后平滑平均 RTT 清零"), Keeper.GetAverageRttMilliseconds(), 0.0f);
    TestEqual(TEXT("Reset 后静默计时清零"), Keeper.GetSecondsSinceLastInbound(), 0.0f);

    FNetHeartbeatConfig DisabledCfg = Config;
    DisabledCfg.bEnabled = false;
    Keeper.SetConfig(DisabledCfg);
    TestFalse(TEXT("心跳配置更新为禁用"), Keeper.GetConfig().bEnabled);
    TestEqual(TEXT("禁用状态下长时步进始终返回 None"), Keeper.Tick(100.0f), ENetHeartbeatAction::None);

    return true;
}

#endif
