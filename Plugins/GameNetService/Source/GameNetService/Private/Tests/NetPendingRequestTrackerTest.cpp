/**
 * @file NetPendingRequestTrackerTest.cpp
 * @brief 异步请求序列号分配、期望响应匹配、超时扫描与断线统一结算器（FNetPendingRequestTracker）单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dispatch/NetPendingRequestTracker.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetPendingRequestTrackerTest,
    "Game.Net.Service.PendingRequestTracker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证非零序列号递增、响应协议校验、超时结算与连接关闭全量失败回退
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetPendingRequestTrackerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FNetPendingRequestTracker Tracker;

    const uint32 Seq1 = Tracker.AllocateSequenceId();
    const uint32 Seq2 = Tracker.AllocateSequenceId();
    TestEqual(TEXT("首个分配的请求序列号为 1"), Seq1, 1u);
    TestEqual(TEXT("第二个分配的请求序列号递增为 2"), Seq2, 2u);

    Tracker.RegisterPendingRequest(0, 2000, 5.0f, [](ENetRequestResult, const FNetPacketHeader&, TConstArrayView<uint8>) {});
    Tracker.RegisterPendingRequest(Seq1, 2000, 5.0f, nullptr);
    TestEqual(TEXT("序列号为 0 或空回调的挂起请求被安全忽略"), Tracker.GetPendingCount(), 0);

    FNetPacketHeader ZeroSeqHeader;
    ZeroSeqHeader.MessageId = 2001;
    ZeroSeqHeader.SequenceId = 0;
    TestFalse(TEXT("SequenceId 为 0 的主动推送包不参与请求结算"), Tracker.TryCompleteResponse(ZeroSeqHeader, {}));

    ENetRequestResult Req1Result = ENetRequestResult::SendFailed;
    uint8 Req1Value = 0;
    Tracker.RegisterPendingRequest(
        Seq1,
        2001,
        5.0f,
        [&Req1Result, &Req1Value](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8> Payload) {
            Req1Result = Result;
            Req1Value = Payload.IsEmpty() ? 0 : Payload[0];
        }
    );

    ENetRequestResult Req2Result = ENetRequestResult::Success;
    Tracker.RegisterPendingRequest(
        Seq2,
        2002,
        1.0f,
        [&Req2Result](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            Req2Result = Result;
        }
    );
    TestEqual(TEXT("当前挂起 2 条异步请求"), Tracker.GetPendingCount(), 2);

    FNetPacketHeader UnknownSeqRsp;
    UnknownSeqRsp.MessageId = 2001;
    UnknownSeqRsp.SequenceId = 999;
    TestFalse(TEXT("未知 SequenceId 不命中挂起请求"), Tracker.TryCompleteResponse(UnknownSeqRsp, {}));

    FNetPacketHeader WrongMsgIdRsp;
    WrongMsgIdRsp.MessageId = 9999;
    WrongMsgIdRsp.SequenceId = Seq1;
    TestFalse(TEXT("SequenceId 相同但协议 ID 不符合预期的包不误消挂起请求"), Tracker.TryCompleteResponse(WrongMsgIdRsp, {}));
    TestEqual(TEXT("挂起请求仍保持 2 条"), Tracker.GetPendingCount(), 2);

    FNetPacketHeader MatchedRsp;
    MatchedRsp.MessageId = 2001;
    MatchedRsp.SequenceId = Seq1;
    const uint8 RspBytes[] = {88};
    TestTrue(TEXT("匹配 SequenceId 与期望协议 ID 的响应成功结算 Req1"), Tracker.TryCompleteResponse(MatchedRsp, RspBytes));
    TestEqual(TEXT("Req1 状态为 Success"), Req1Result, ENetRequestResult::Success);
    TestEqual(TEXT("Req1 收到负载字节 88"), Req1Value, static_cast<uint8>(88));
    TestEqual(TEXT("结算后剩余 1 条挂起请求"), Tracker.GetPendingCount(), 1);

    Tracker.TickTimeouts(0.0f);
    Tracker.TickTimeouts(0.5f);
    TestEqual(TEXT("推进 0.5s 未达 Req2 超时阈值"), Tracker.GetPendingCount(), 1);
    Tracker.TickTimeouts(0.6f);
    TestEqual(TEXT("跨过 1.0s 后 Req2 触发 TimedOut 自动结算"), Req2Result, ENetRequestResult::TimedOut);
    TestEqual(TEXT("超时清理后挂起请求归零"), Tracker.GetPendingCount(), 0);

    ENetRequestResult Req3Result = ENetRequestResult::Success;
    const uint32 Seq3 = Tracker.AllocateSequenceId();
    Tracker.RegisterPendingRequest(
        Seq3,
        0,
        10.0f,
        [&Req3Result](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            Req3Result = Result;
        }
    );
    Tracker.FailAll(ENetRequestResult::ConnectionClosed);
    TestEqual(TEXT("FailAll 时未完成请求收到 ConnectionClosed 结算保障零悬挂"), Req3Result, ENetRequestResult::ConnectionClosed);
    TestEqual(TEXT("FailAll 后挂起请求数为 0"), Tracker.GetPendingCount(), 0);

    return true;
}

#endif

