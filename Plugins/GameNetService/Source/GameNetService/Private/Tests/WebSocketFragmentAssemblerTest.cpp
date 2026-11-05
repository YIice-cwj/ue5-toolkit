/**
 * @file WebSocketFragmentAssemblerTest.cpp
 * @brief WebSocket 二进制分片消息重组器与最大帧长防溢出保护（FWebSocketFragmentAssembler）单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Transport/WebSocketFragmentAssembler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetWebSocketFragmentAssemblerTest,
    "Game.Net.Service.WebSocketFragmentAssembler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单分片直通、多分片累加拼装、超限分片自动清空熔断与 Reset 行为
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetWebSocketFragmentAssemblerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FWebSocketFragmentAssembler Assembler(16);
    TestEqual(TEXT("最大帧长等于 16 字节报文头加 16 字节最大负载"), Assembler.GetMaxFrameBytes(), int64(32));
    TestEqual(TEXT("初始缓存分片字节为 0"), Assembler.GetBufferedFragmentBytes(), 0);

    const uint8 SingleChunk[] = {1, 2, 3, 4};
    const FWebSocketFragmentOutcome DirectOutcome = Assembler.AppendFragment(SingleChunk, true);
    TestEqual(TEXT("单分片完整消息立即返回 MessageReady"), DirectOutcome.Status, EWebSocketFragmentStatus::MessageReady);
    TestEqual(TEXT("单分片消息长度一致"), DirectOutcome.AssembledMessage.Num(), 4);
    TestEqual(TEXT("单分片直通不残留缓存"), Assembler.GetBufferedFragmentBytes(), 0);

    const uint8 Part1[] = {10, 20, 30};
    const uint8 Part2[] = {40, 50};
    const uint8 Part3[] = {60};
    TestEqual(
        TEXT("首分片未结束返回 Incomplete"),
        Assembler.AppendFragment(Part1, false).Status,
        EWebSocketFragmentStatus::Incomplete
    );
    TestEqual(
        TEXT("中间分片未结束返回 Incomplete"),
        Assembler.AppendFragment(Part2, false).Status,
        EWebSocketFragmentStatus::Incomplete
    );
    TestEqual(TEXT("已累积 5 字节未完成分片"), Assembler.GetBufferedFragmentBytes(), 5);

    const FWebSocketFragmentOutcome MultiOutcome = Assembler.AppendFragment(Part3, true);
    TestEqual(TEXT("末分片到达产出 MessageReady"), MultiOutcome.Status, EWebSocketFragmentStatus::MessageReady);
    TestEqual(TEXT("重组后总字节数为 6"), MultiOutcome.AssembledMessage.Num(), 6);
    TestEqual(TEXT("重组后末字节为 60"), MultiOutcome.AssembledMessage[5], static_cast<uint8>(60));
    TestEqual(TEXT("重组完成后内部缓存归零"), Assembler.GetBufferedFragmentBytes(), 0);

    TArray<uint8> TwentyBytes;
    TwentyBytes.SetNumZeroed(20);
    TestEqual(
        TEXT("追加 20 字节首分片未超 32 字节上限"),
        Assembler.AppendFragment(TwentyBytes, false).Status,
        EWebSocketFragmentStatus::Incomplete
    );
    const FWebSocketFragmentOutcome OverflowOutcome = Assembler.AppendFragment(TwentyBytes, true);
    TestEqual(
        TEXT("累计 40 字节超过 32 字节最大帧长触发 FrameExceedsLimit"),
        OverflowOutcome.Status,
        EWebSocketFragmentStatus::FrameExceedsLimit
    );
    TestEqual(TEXT("超限拦截后自动清空半包缓存"), Assembler.GetBufferedFragmentBytes(), 0);

    return true;
}

#endif

