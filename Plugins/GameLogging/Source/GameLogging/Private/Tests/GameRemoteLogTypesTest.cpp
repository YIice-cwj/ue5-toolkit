// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameRemoteLogTypesTest.cpp
 * @brief FGameRemoteLogEntry 数据契约结构体、级别掩码与 TAG_Game_Log_Remote 单元测试
 */

#include "Remote/GameRemoteLogTypes.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRemoteLogTypesTest,
    "Game.Logging.RemoteTypes.EntryContractAndTag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TAG_Game_Log_Remote 合法性与 FGameRemoteLogEntry 构造、掩码和大小写敏感相等性契约
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true
 */
bool FGameRemoteLogTypesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag RemoteTag = TAG_Game_Log_Remote.GetTag();
    TestTrue(TEXT("TAG_Game_Log_Remote 原生标签应有效"), RemoteTag.IsValid());
    TestEqual(TEXT("TAG_Game_Log_Remote 标签名应为 Game.Log.Remote"), RemoteTag.GetTagName(), FName(TEXT("Game.Log.Remote")));

    const FGameRemoteLogEntry DefaultEntry;
    TestEqual(TEXT("默认构造分类应为 NAME_None"), DefaultEntry.Category, FName(NAME_None));
    TestEqual(TEXT("默认构造详细级别应为 Log"), DefaultEntry.GetVerbosity(), ELogVerbosity::Log);
    TestTrue(TEXT("默认构造正文应为空"), DefaultEntry.Message.IsEmpty());
    TestEqual(TEXT("默认构造时间戳应为 0.0"), DefaultEntry.Timestamp, 0.0);
    TestEqual(TEXT("默认构造帧号应为 0"), DefaultEntry.FrameNumber, int64(0));

    const uint8 VerbosityWithFlags = static_cast<uint8>(ELogVerbosity::Warning | ELogVerbosity::BreakOnLog);
    const FGameRemoteLogEntry EntryA(
        FName(TEXT("LogCombat")),
        VerbosityWithFlags,
        TEXT("Critical Damage: ABC"),
        123.456,
        9001
    );
    TestEqual(TEXT("构造函数应剥离非详细级别标志位"), EntryA.Verbosity, static_cast<uint8>(ELogVerbosity::Warning));
    TestEqual(TEXT("GetVerbosity 应返回纯 Warning 枚举"), EntryA.GetVerbosity(), ELogVerbosity::Warning);
    TestEqual(TEXT("Category 字段正确"), EntryA.Category, FName(TEXT("LogCombat")));
    TestEqual(TEXT("Message 字段正确"), EntryA.Message, FString(TEXT("Critical Damage: ABC")));
    TestEqual(TEXT("Timestamp 字段正确"), EntryA.Timestamp, 123.456);
    TestEqual(TEXT("FrameNumber 字段正确"), EntryA.FrameNumber, int64(9001));

    const FGameRemoteLogEntry EntryExactCopy(
        FName(TEXT("LogCombat")),
        static_cast<uint8>(ELogVerbosity::Warning),
        TEXT("Critical Damage: ABC"),
        123.456,
        9001
    );
    TestTrue(TEXT("字段全等的两个条目应相等"), EntryA == EntryExactCopy);

    const FGameRemoteLogEntry EntryDifferentCase(
        FName(TEXT("LogCombat")),
        static_cast<uint8>(ELogVerbosity::Warning),
        TEXT("critical damage: abc"),
        123.456,
        9001
    );
    TestFalse(TEXT("正文大小写不同时 operator== 必须返回 false"), EntryA == EntryDifferentCase);

    const FGameRemoteLogEntry EntryDiffCategory(
        FName(TEXT("LogOther")),
        static_cast<uint8>(ELogVerbosity::Warning),
        TEXT("Critical Damage: ABC"),
        123.456,
        9001
    );
    TestFalse(TEXT("分类不同应不相等"), EntryA == EntryDiffCategory);

    const FGameRemoteLogEntry EntryDiffVerbosity(
        FName(TEXT("LogCombat")),
        static_cast<uint8>(ELogVerbosity::Error),
        TEXT("Critical Damage: ABC"),
        123.456,
        9001
    );
    TestFalse(TEXT("级别不同应不相等"), EntryA == EntryDiffVerbosity);

    const FGameRemoteLogEntry EntryDiffTimestamp(
        FName(TEXT("LogCombat")),
        static_cast<uint8>(ELogVerbosity::Warning),
        TEXT("Critical Damage: ABC"),
        999.0,
        9001
    );
    TestFalse(TEXT("时间戳不同应不相等"), EntryA == EntryDiffTimestamp);

    const FGameRemoteLogEntry EntryDiffFrame(
        FName(TEXT("LogCombat")),
        static_cast<uint8>(ELogVerbosity::Warning),
        TEXT("Critical Damage: ABC"),
        123.456,
        9002
    );
    TestFalse(TEXT("帧号不同应不相等"), EntryA == EntryDiffFrame);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
