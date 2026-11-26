/**
 * @file FGameCheatRegistry.cpp
 * @brief 受编译期 GM 开关保护的轻量模块化调试/GM 指令注册中心实现
 */

#include "Cheat/FGameCheatRegistry.h"

#include "Config/GameCompileSwitches.h"
#include "HAL/IConsoleManager.h"
#include "Logging/GameLogCategories.h"
#include "Misc/ScopeLock.h"

namespace {

bool IsValidCheatCommandName(FStringView CommandView) noexcept {
    if (CommandView.IsEmpty()) {
        return false;
    }
    for (const TCHAR Ch : CommandView) {
        if (FChar::IsWhitespace(Ch)) {
            return false;
        }
    }
    return true;
}

bool ExtractLeadingToken(FStringView CommandLine, FStringView& OutCommand, FStringView& OutRemainder) noexcept {
    const int32 Length = CommandLine.Len();
    int32 StartIndex = 0;
    while (StartIndex < Length && FChar::IsWhitespace(CommandLine[StartIndex])) {
        ++StartIndex;
    }
    if (StartIndex >= Length) {
        return false;
    }

    int32 EndIndex = StartIndex;
    while (EndIndex < Length && !FChar::IsWhitespace(CommandLine[EndIndex])) {
        ++EndIndex;
    }

    OutCommand = CommandLine.SubStr(StartIndex, EndIndex - StartIndex);
    OutRemainder = CommandLine.RightChop(EndIndex);
    return true;
}

void TokenizeCheatArguments(FStringView ArgsView, TArray<FString>& OutArgs) {
    OutArgs.Reset();
    if (ArgsView.IsEmpty()) {
        return;
    }

    FString Current;
    bool bInQuotes = false;
    bool bHasToken = false;
    const TCHAR* Data = ArgsView.GetData();
    const int32 Length = ArgsView.Len();

    int32 Index = 0;
    while (Index < Length) {
        const TCHAR Ch = Data[Index];
        if (Ch == TEXT('\\') && Index + 1 < Length) {
            const TCHAR NextCh = Data[Index + 1];
            if (NextCh == TEXT('\"') || NextCh == TEXT('\\')) {
                Current.AppendChar(NextCh);
                bHasToken = true;
                Index += 2;
                continue;
            }
        }
        if (Ch == TEXT('\"')) {
            bInQuotes = !bInQuotes;
            bHasToken = true;
            ++Index;
            continue;
        }
        if (!bInQuotes && FChar::IsWhitespace(Ch)) {
            if (bHasToken) {
                OutArgs.Add(MoveTemp(Current));
                Current.Reset();
                bHasToken = false;
            }
            ++Index;
            continue;
        }

        const int32 RunStart = Index;
        ++Index;
        while (Index < Length) {
            const TCHAR RunCh = Data[Index];
            if (RunCh == TEXT('\\') || RunCh == TEXT('\"') || (!bInQuotes && FChar::IsWhitespace(RunCh))) {
                break;
            }
            ++Index;
        }
        Current.AppendChars(Data + RunStart, Index - RunStart);
        bHasToken = true;
    }

    if (bHasToken) {
        OutArgs.Add(MoveTemp(Current));
    }
}

void UnregisterOwnedConsoleCommand(
    IConsoleManager& ConsoleManager,
    FName CommandName,
    IConsoleObject* ExpectedConsoleObj
) {
    if (ExpectedConsoleObj == nullptr || CommandName.IsNone()) {
        return;
    }
    TCHAR NameBuffer[FName::StringBufferSize];
    CommandName.ToString(NameBuffer);
    if (ConsoleManager.FindConsoleObject(NameBuffer) == ExpectedConsoleObj) {
        ConsoleManager.UnregisterConsoleObject(NameBuffer, false);
    }
}

}  // namespace

bool FGameCheatRegistry::RegisterCheat(FName CommandName, const FString& HelpText, FCheatHandler Handler) {
    if constexpr (!Game::Config::bEnableGM) {
        return false;
    }

    if (CommandName.IsNone() || !Handler) {
        return false;
    }

    TCHAR NameBuffer[FName::StringBufferSize];
    const uint32 NameLen = CommandName.ToString(NameBuffer);
    const FStringView CommandView(NameBuffer, static_cast<int32>(NameLen));
    if (!IsValidCheatCommandName(CommandView)) {
        GAME_LOG(Warning, TEXT("【GameDebug】【Cheat】指令名称 [%.*s] 包含空白或为空，拒绝注册。"), CommandView.Len(), CommandView.GetData());
        return false;
    }

    TSharedPtr<const FCheatHandler> SharedHandler = MakeShared<const FCheatHandler>(MoveTemp(Handler));
    FScopeLock SyncLock(&ConsoleSyncLock);
    {
        FReadScopeLock ReadLock(RegistryLock);
        if (Entries.Contains(CommandName)) {
            GAME_LOG(Warning, TEXT("【GameDebug】【Cheat】指令 [%.*s] 已存在，拒绝重复注册。"), CommandView.Len(), CommandView.GetData());
            return false;
        }
    }

    IConsoleObject* ConsoleObj = nullptr;
    bool bHadConsoleConflict = false;
    IConsoleManager& ConsoleManager = IConsoleManager::Get();
    if (ConsoleManager.FindConsoleObject(NameBuffer) == nullptr) {
        ConsoleObj = ConsoleManager.RegisterConsoleCommand(
            NameBuffer,
            *HelpText,
            FConsoleCommandWithArgsDelegate::CreateLambda([SharedHandler](const TArray<FString>& Args) {
                (*SharedHandler)(Args);
            }),
            ECVF_Cheat
        );
    } else {
        bHadConsoleConflict = true;
    }

    {
        FWriteScopeLock WriteLock(RegistryLock);
        Entries.Add(CommandName, FCheatEntry{SharedHandler, ConsoleObj});
    }

    if (bHadConsoleConflict) {
        GAME_LOG(
            Warning,
            TEXT("【GameDebug】【Cheat】引擎控制台已存在同名对象 [%.*s]，仅通过 ExecuteCheat 触发回调。"),
            CommandView.Len(),
            CommandView.GetData()
        );
    }

    GAME_LOG(Verbose, TEXT("【GameDebug】【Cheat】已注册 GM 指令 [%.*s]。"), CommandView.Len(), CommandView.GetData());
    return true;
}

bool FGameCheatRegistry::UnregisterCheat(FName CommandName) {
    if constexpr (!Game::Config::bEnableGM) {
        return false;
    }

    if (CommandName.IsNone()) {
        return false;
    }

    FScopeLock SyncLock(&ConsoleSyncLock);
    IConsoleObject* ConsoleToRemove = nullptr;
    bool bRemoved = false;
    {
        FWriteScopeLock WriteLock(RegistryLock);
        FCheatEntry RemovedEntry;
        if (Entries.RemoveAndCopyValue(CommandName, RemovedEntry)) {
            ConsoleToRemove = RemovedEntry.ConsoleCommand;
            bRemoved = true;
        }
    }

    UnregisterOwnedConsoleCommand(IConsoleManager::Get(), CommandName, ConsoleToRemove);
    if (bRemoved) {
        GAME_LOG(Verbose, TEXT("【GameDebug】【Cheat】已注销 GM 指令 [%s]。"), *CommandName.ToString());
    }
    return bRemoved;
}

bool FGameCheatRegistry::ExecuteCheat(FStringView CommandLine) {
    if constexpr (!Game::Config::bEnableGM) {
        return false;
    }

    FStringView CommandView;
    FStringView ArgsView;
    if (!ExtractLeadingToken(CommandLine, CommandView, ArgsView)) {
        return false;
    }

    const FName CommandName(CommandView, FNAME_Find);
    if (CommandName.IsNone()) {
        return false;
    }

    TSharedPtr<const FCheatHandler> HandlerToInvoke;
    {
        FReadScopeLock ReadLock(RegistryLock);
        if (const FCheatEntry* Entry = Entries.Find(CommandName)) {
            HandlerToInvoke = Entry->Handler;
        }
    }

    if (!HandlerToInvoke.IsValid()) {
        return false;
    }

    TArray<FString> Args;
    TokenizeCheatArguments(ArgsView, Args);
    (*HandlerToInvoke)(Args);
    return true;
}

bool FGameCheatRegistry::HasCheat(FName CommandName) const {
    if constexpr (!Game::Config::bEnableGM) {
        return false;
    }

    if (CommandName.IsNone()) {
        return false;
    }

    FReadScopeLock ReadLock(RegistryLock);
    return Entries.Contains(CommandName);
}

int32 FGameCheatRegistry::GetCheatCount() const {
    if constexpr (!Game::Config::bEnableGM) {
        return 0;
    }

    FReadScopeLock ReadLock(RegistryLock);
    return Entries.Num();
}

void FGameCheatRegistry::ClearAll() {
    if constexpr (!Game::Config::bEnableGM) {
        return;
    }

    FScopeLock SyncLock(&ConsoleSyncLock);
    TArray<TPair<FName, IConsoleObject*>> ConsoleObjectsToRemove;
    int32 ClearedCount = 0;
    {
        FWriteScopeLock WriteLock(RegistryLock);
        ClearedCount = Entries.Num();
        ConsoleObjectsToRemove.Reserve(ClearedCount);
        for (TPair<FName, FCheatEntry>& Pair : Entries) {
            if (Pair.Value.ConsoleCommand != nullptr) {
                ConsoleObjectsToRemove.Emplace(Pair.Key, Pair.Value.ConsoleCommand);
            }
        }
        Entries.Empty();
    }

    IConsoleManager& ConsoleManager = IConsoleManager::Get();
    for (const TPair<FName, IConsoleObject*>& Item : ConsoleObjectsToRemove) {
        UnregisterOwnedConsoleCommand(ConsoleManager, Item.Key, Item.Value);
    }

    if (ClearedCount > 0) {
        GAME_LOG(Verbose, TEXT("【GameDebug】【Cheat】已清空全部 GM 指令，共计 %d 条。"), ClearedCount);
    }
}

FGameCheatRegistry& FGameCheatRegistry::Get() {
    static FGameCheatRegistry Instance;
    return Instance;
}
