/**
 * @file NetPendingRequestTrackerTest.cpp
 * @brief 异步请求序列号分配、单请求取消、重入超时扫描与断线统一结算器（FNetPendingRequestTracker）单元测试
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
 * @brief 验证非零序列号递增、响应协议校验、单请求取消、同序号覆盖结算、回调重入 Reset/FailAll 安全与全量失败回退
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
    TestFalse(TEXT("取消不存在的 0 号请求返回 false"), Tracker.CancelRequest(0));
    TestFalse(TEXT("取消未知序号请求返回 false"), Tracker.CancelRequest(999));

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

    ENetRequestResult CancelTargetResult = ENetRequestResult::Success;
    const uint32 CancelSeq = Tracker.AllocateSequenceId();
    Tracker.RegisterPendingRequest(
        CancelSeq,
        2003,
        10.0f,
        [&CancelTargetResult](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            CancelTargetResult = Result;
        }
    );
    TestTrue(TEXT("主动 CancelRequest 命中挂起请求"), Tracker.CancelRequest(CancelSeq));
    TestEqual(TEXT("被取消请求回调精确收到 Cancelled"), CancelTargetResult, ENetRequestResult::Cancelled);
    TestEqual(TEXT("取消后挂起请求归零"), Tracker.GetPendingCount(), 0);

    ENetRequestResult EvictedResult = ENetRequestResult::Success;
    ENetRequestResult ReplacementResult = ENetRequestResult::SendFailed;
    const uint32 DupSeq = Tracker.AllocateSequenceId();
    Tracker.RegisterPendingRequest(
        DupSeq,
        2004,
        10.0f,
        [&EvictedResult](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            EvictedResult = Result;
        }
    );
    Tracker.RegisterPendingRequest(
        DupSeq,
        2004,
        10.0f,
        [&ReplacementResult](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            ReplacementResult = Result;
        }
    );
    TestEqual(TEXT("重复注册同 SequenceId 时旧条目立即收到 Cancelled 防止协程永久挂死"), EvictedResult, ENetRequestResult::Cancelled);
    Tracker.FailAll(ENetRequestResult::ConnectionClosed);
    TestEqual(TEXT("FailAll 时未完成请求收到 ConnectionClosed 结算"), ReplacementResult, ENetRequestResult::ConnectionClosed);

    int32 ReentrantTimeoutHits = 0;
    const uint32 ReentrantSeqA = Tracker.AllocateSequenceId();
    const uint32 ReentrantSeqB = Tracker.AllocateSequenceId();
    Tracker.RegisterPendingRequest(
        ReentrantSeqA,
        0,
        0.5f,
        [&Tracker, &ReentrantTimeoutHits](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            if (Result == ENetRequestResult::TimedOut) {
                ++ReentrantTimeoutHits;
                Tracker.Reset();
            }
        }
    );
    Tracker.RegisterPendingRequest(
        ReentrantSeqB,
        0,
        0.5f,
        [&ReentrantTimeoutHits](ENetRequestResult Result, const FNetPacketHeader&, TConstArrayView<uint8>) {
            if (Result == ENetRequestResult::TimedOut) {
                ++ReentrantTimeoutHits;
            }
        }
    );
    Tracker.TickTimeouts(1.0f);
    TestEqual(TEXT("超时回调内重入 Reset 不破坏局部快照，两条到期请求均安全结算"), ReentrantTimeoutHits, 2);
    TestEqual(TEXT("重入后挂起请求归零"), Tracker.GetPendingCount(), 0);

    return true;
}

#endif
