/**
 * @file FGameCheatRegistry.cpp
 * @brief 受编译期 GM 开关保护的轻量模块化调试/GM 指令注册中心实现
 */

#include "Debug/FGameCheatRegistry.h"

#include "Config/GameCompileSwitches.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCheat, Log, All);

namespace {

void TokenizeCheatCommandLine(const FString& CommandLine, TArray<FString>& OutTokens) {
    OutTokens.Reset();
    FString Current;
    bool bInQuotes = false;

    for (int32 Index = 0; Index < CommandLine.Len(); ++Index) {
        const TCHAR Ch = CommandLine[Index];
        if (Ch == TEXT('\"')) {
            bInQuotes = !bInQuotes;
            continue;
        }
        if (!bInQuotes && FChar::IsWhitespace(Ch)) {
            if (!Current.IsEmpty()) {
                OutTokens.Add(MoveTemp(Current));
                Current.Reset();
            }
            continue;
        }
        Current.AppendChar(Ch);
    }

    if (!Current.IsEmpty()) {
        OutTokens.Add(MoveTemp(Current));
    }
}

}  // namespace

FGameCheatRegistry& FGameCheatRegistry::Get() {
    static FGameCheatRegistry Instance;
    return Instance;
}

bool FGameCheatRegistry::RegisterCheat(
    const FName& CommandName,
    const FString& HelpText,
    FCheatHandler Handler) {
    if constexpr (!Game::Config::bEnableGM) {
        return false;
    } else {
        if (CommandName.IsNone() || !Handler) {
            return false;
        }

        const FString CommandString = CommandName.ToString().TrimStartAndEnd();
        if (CommandString.IsEmpty()) {
            return false;
        }

        TSharedPtr<const FCheatHandler> SharedHandler = MakeShared<const FCheatHandler>(MoveTemp(Handler));
        FWriteScopeLock WriteLock(RegistryLock);
        if (Entries.Contains(CommandName)) {
            if constexpr (Game::Config::bEnableLog) {
                UE_LOG(LogGameCheat, Warning, TEXT("【GM注册表】指令 [%s] 已存在，拒绝重复注册。"), *CommandString);
            }
            return false;
        }

        IConsoleObject* ConsoleObj = nullptr;
        IConsoleManager& ConsoleManager = IConsoleManager::Get();
        if (ConsoleManager.FindConsoleObject(*CommandString) == nullptr) {
            ConsoleObj = ConsoleManager.RegisterConsoleCommand(
                *CommandString,
                *HelpText,
                FConsoleCommandWithArgsDelegate::CreateLambda([SharedHandler](const TArray<FString>& Args) {
                    (*SharedHandler)(Args);
                }),
                ECVF_Cheat);
        }

        Entries.Add(CommandName, FCheatEntry{HelpText, MoveTemp(SharedHandler), ConsoleObj});
        return true;
    }
}

bool FGameCheatRegistry::UnregisterCheat(const FName& CommandName) {
    if constexpr (!Game::Config::bEnableGM) {
        return false;
    } else {
        if (CommandName.IsNone()) {
            return false;
        }

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

        if (ConsoleToRemove != nullptr) {
            IConsoleManager::Get().UnregisterConsoleObject(ConsoleToRemove, false);
        }
        return bRemoved;
    }
}

bool FGameCheatRegistry::ExecuteCheat(const FString& CommandLine) {
    if constexpr (!Game::Config::bEnableGM) {
        return false;
    } else {
        TArray<FString> Tokens;
        TokenizeCheatCommandLine(CommandLine, Tokens);
        if (Tokens.IsEmpty()) {
            return false;
        }

        const FName CommandName(*Tokens[0]);
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
        if (Tokens.Num() > 1) {
            Args.Reserve(Tokens.Num() - 1);
            for (int32 Index = 1; Index < Tokens.Num(); ++Index) {
                Args.Add(MoveTemp(Tokens[Index]));
            }
        }

        (*HandlerToInvoke)(Args);
        return true;
    }
}

bool FGameCheatRegistry::HasCheat(const FName& CommandName) const {
    if constexpr (!Game::Config::bEnableGM) {
        return false;
    } else {
        if (CommandName.IsNone()) {
            return false;
        }
        FReadScopeLock ReadLock(RegistryLock);
        return Entries.Contains(CommandName);
    }
}

int32 FGameCheatRegistry::GetCheatCount() const {
    if constexpr (!Game::Config::bEnableGM) {
        return 0;
    } else {
        FReadScopeLock ReadLock(RegistryLock);
        return Entries.Num();
    }
}

void FGameCheatRegistry::ClearAll() {
    if constexpr (Game::Config::bEnableGM) {
        TArray<IConsoleObject*> ConsoleObjectsToRemove;
        {
            FWriteScopeLock WriteLock(RegistryLock);
            ConsoleObjectsToRemove.Reserve(Entries.Num());
            for (TPair<FName, FCheatEntry>& Pair : Entries) {
                if (Pair.Value.ConsoleCommand != nullptr) {
                    ConsoleObjectsToRemove.Add(Pair.Value.ConsoleCommand);
                }
            }
            Entries.Empty();
        }

        IConsoleManager& ConsoleManager = IConsoleManager::Get();
        for (IConsoleObject* ConsoleObj : ConsoleObjectsToRemove) {
            ConsoleManager.UnregisterConsoleObject(ConsoleObj, false);
        }
    }
}
