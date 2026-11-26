/**
 * @file FGameDebugWatchHub.cpp
 * @brief 现代化通用运行时调试观察中心实现
 */

#include "Watcher/FGameDebugWatchHub.h"

#include "Cheat/FGameCheatRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Core/GameDebugScreenKeys.h"
#include "Engine/Engine.h"
#include "Logging/GameLogCategories.h"
#include "Misc/StringBuilder.h"
#include "Print/FGameDebugPrint.h"

namespace {

inline constexpr const TCHAR* CmdWatchName = TEXT("GM.Watch");
inline constexpr float MinDisplayDurationSeconds = 0.1f;

bool TryParseToggleFlag(FStringView Value, bool& OutEnabled) noexcept {
    if (Value.Equals(TEXT("on"), ESearchCase::IgnoreCase)
        || Value.Equals(TEXT("1"), ESearchCase::CaseSensitive)
        || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase)) {
        OutEnabled = true;
        return true;
    }
    if (Value.Equals(TEXT("off"), ESearchCase::IgnoreCase)
        || Value.Equals(TEXT("0"), ESearchCase::CaseSensitive)
        || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase)) {
        OutEnabled = false;
        return true;
    }
    return false;
}

void RemoveCategoryScreenMessage(FName Category) {
    if (GEngine == nullptr || Category.IsNone()) {
        return;
    }
    GEngine->RemoveOnScreenDebugMessage(Game::Debug::Private::MakeWatchHubCategoryKey(Category));
}

}  // namespace

void FGameDebugWatchHub::Initialize() {
    if constexpr (!Game::Config::bEnableGM || !Game::Config::bEnableDebugDraw) {
        return;
    }

    FGameCheatRegistry& Registry = FGameCheatRegistry::Get();
    const FName CmdWatch(CmdWatchName);
    if (!Registry.HasCheat(CmdWatch)) {
        Registry.RegisterCheat(
            CmdWatch,
            TEXT("控制调试观察中心显示状态。用法: GM.Watch list | GM.Watch all <on/off> | GM.Watch <Category> <on/off>"),
            [this](const TArray<FString>& Args) {
                HandleWatchCommand(Args);
            }
        );
    }
}

void FGameDebugWatchHub::Shutdown() {
    if constexpr (Game::Config::bEnableGM) {
        FGameCheatRegistry::Get().UnregisterCheat(FName(CmdWatchName));
    }
    ClearAll();
    bGlobalEnabled.store(true, std::memory_order_relaxed);
}

bool FGameDebugWatchHub::Watch(FName Category, FName Name, FValueGetter Getter) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return false;
    }

    if (Category.IsNone() || Name.IsNone() || !Getter) {
        return false;
    }

    TSharedPtr<const FValueGetter> SharedGetter = MakeShared<const FValueGetter>(MoveTemp(Getter));
    FWriteScopeLock WriteLock(RegistryLock);
    FCategoryGroup& Group = Categories.FindOrAdd(Category);

    for (FWatchItem& Item : Group.Items) {
        if (Item.Name == Name) {
            Item.Getter = MoveTemp(SharedGetter);
            return true;
        }
    }

    Group.Items.Add(FWatchItem{Name, MoveTemp(SharedGetter)});
    return true;
}

bool FGameDebugWatchHub::Unwatch(FName Category, FName Name) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return false;
    }

    if (Category.IsNone() || Name.IsNone()) {
        return false;
    }

    bool bBecameEmpty = false;
    {
        FWriteScopeLock WriteLock(RegistryLock);
        FCategoryGroup* Group = Categories.Find(Category);
        if (Group == nullptr) {
            return false;
        }

        const int32 FoundIndex = Group->Items.IndexOfByPredicate([Name](const FWatchItem& Item) {
            return Item.Name == Name;
        });
        if (FoundIndex == INDEX_NONE) {
            return false;
        }

        Group->Items.RemoveAt(FoundIndex);
        bBecameEmpty = Group->Items.IsEmpty();
        if (bBecameEmpty && Group->bEnabled) {
            Categories.Remove(Category);
        }
    }

    if (bBecameEmpty) {
        RemoveCategoryScreenMessage(Category);
    }
    return true;
}

void FGameDebugWatchHub::SetCategoryEnabled(FName Category, bool bEnabled) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    }

    if (Category.IsNone()) {
        return;
    }

    {
        FWriteScopeLock WriteLock(RegistryLock);
        FCategoryGroup& Group = Categories.FindOrAdd(Category);
        Group.bEnabled = bEnabled;
    }

    if (!bEnabled) {
        RemoveCategoryScreenMessage(Category);
    }
    GAME_LOG(Verbose, TEXT("【GameDebug】【WatchHub】分类 [%s] 显示开关设为 %s。"), *Category.ToString(), bEnabled ? TEXT("ON") : TEXT("OFF"));
}

bool FGameDebugWatchHub::IsCategoryEnabled(FName Category) const {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return false;
    }

    if (Category.IsNone()) {
        return false;
    }

    FReadScopeLock ReadLock(RegistryLock);
    if (const FCategoryGroup* Group = Categories.Find(Category)) {
        return Group->bEnabled;
    }
    return false;
}

void FGameDebugWatchHub::SetGlobalEnabled(bool bEnabled) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    }

    bGlobalEnabled.store(bEnabled, std::memory_order_relaxed);
    if (!bEnabled && GEngine != nullptr) {
        TArray<FName, TInlineAllocator<16>> CategoriesToHide;
        {
            FReadScopeLock ReadLock(RegistryLock);
            CategoriesToHide.Reserve(Categories.Num());
            for (const TPair<FName, FCategoryGroup>& Pair : Categories) {
                CategoriesToHide.Add(Pair.Key);
            }
        }
        for (const FName Category : CategoriesToHide) {
            RemoveCategoryScreenMessage(Category);
        }
    }
}

bool FGameDebugWatchHub::IsGlobalEnabled() const noexcept {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return false;
    }

    return bGlobalEnabled.load(std::memory_order_relaxed);
}

void FGameDebugWatchHub::GetCategories(TArray<FName>& OutCategories) const {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        OutCategories.Reset();
        return;
    }

    FReadScopeLock ReadLock(RegistryLock);
    Categories.GetKeys(OutCategories);
}

int32 FGameDebugWatchHub::GetWatchItemCount(FName Category) const {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return 0;
    }

    if (Category.IsNone()) {
        return 0;
    }

    FReadScopeLock ReadLock(RegistryLock);
    if (const FCategoryGroup* Group = Categories.Find(Category)) {
        return Group->Items.Num();
    }
    return 0;
}

void FGameDebugWatchHub::ClearAll() {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    }

    TArray<FName, TInlineAllocator<16>> ClearedKeys;
    {
        FWriteScopeLock WriteLock(RegistryLock);
        ClearedKeys.Reserve(Categories.Num());
        for (const TPair<FName, FCategoryGroup>& Pair : Categories) {
            ClearedKeys.Add(Pair.Key);
        }
        Categories.Empty();
    }

    for (const FName Category : ClearedKeys) {
        RemoveCategoryScreenMessage(Category);
    }

    if (!ClearedKeys.IsEmpty()) {
        GAME_LOG(Verbose, TEXT("【GameDebug】【WatchHub】已清空全部观察分类，共计 %d 组。"), ClearedKeys.Num());
    }
}

void FGameDebugWatchHub::TickDisplay(float DeltaTime) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    }

    if (!bGlobalEnabled.load(std::memory_order_relaxed)) {
        return;
    }

    struct FCategorySnapshot {
        FName Category;
        TArray<FWatchItem, TInlineAllocator<16>> Items;
    };

    TArray<FCategorySnapshot, TInlineAllocator<8>> ActiveSnapshots;
    {
        FReadScopeLock ReadLock(RegistryLock);
        ActiveSnapshots.Reserve(Categories.Num());
        for (const TPair<FName, FCategoryGroup>& Pair : Categories) {
            if (!Pair.Value.bEnabled || Pair.Value.Items.IsEmpty()) {
                continue;
            }
            FCategorySnapshot& Snapshot = ActiveSnapshots.Emplace_GetRef();
            Snapshot.Category = Pair.Key;
            Snapshot.Items.Append(Pair.Value.Items);
        }
    }

    const float SafeDelta = (FMath::IsFinite(DeltaTime) && DeltaTime > 0.0f) ? DeltaTime : 0.0f;
    const float DisplayDuration = FMath::Max(MinDisplayDurationSeconds, SafeDelta * 2.0f);

    TStringBuilder<512> DisplayBuffer;
    TCHAR ItemNameBuf[FName::StringBufferSize];
    for (const FCategorySnapshot& Snapshot : ActiveSnapshots) {
        DisplayBuffer.Reset();
        DisplayBuffer.Append(TEXT("=== [ "));
        Snapshot.Category.AppendString(DisplayBuffer);
        DisplayBuffer.Append(TEXT(" ] ===\n"));

        for (const FWatchItem& Item : Snapshot.Items) {
            Item.Name.ToString(ItemNameBuf);
            const FString ValueString = Item.Getter.IsValid() ? (*Item.Getter)() : FString();
            DisplayBuffer.Appendf(TEXT("  %-16s: %s\n"), ItemNameBuf, *ValueString);
        }

        const uint64 CategoryScreenKey = Game::Debug::Private::MakeWatchHubCategoryKey(Snapshot.Category);
        FGameDebugPrint::Print(FString(DisplayBuffer.ToView()), FColor::Cyan, DisplayDuration, CategoryScreenKey);
    }
}

void FGameDebugWatchHub::HandleWatchCommand(const TArray<FString>& Args) {
    if (Args.IsEmpty()) {
        FGameDebugPrint::PrintWarning(TEXT("用法: GM.Watch list | GM.Watch all <on/off> | GM.Watch <Category> <on/off>"));
        return;
    }

    const FStringView FirstArg(Args[0]);
    if (FirstArg.Equals(TEXT("list"), ESearchCase::IgnoreCase)) {
        struct FCategorySummary {
            FName Category;
            bool bEnabled = true;
            int32 ItemCount = 0;
        };
        TArray<FCategorySummary, TInlineAllocator<16>> Summaries;
        {
            FReadScopeLock ReadLock(RegistryLock);
            Summaries.Reserve(Categories.Num());
            for (const TPair<FName, FCategoryGroup>& Pair : Categories) {
                Summaries.Add(FCategorySummary{Pair.Key, Pair.Value.bEnabled, Pair.Value.Items.Num()});
            }
        }

        TStringBuilder<512> Summary;
        Summary.Appendf(
            TEXT("【调试观察中心】全局: %s, 当前分类总数: %d\n"),
            IsGlobalEnabled() ? TEXT("ON") : TEXT("OFF"),
            Summaries.Num()
        );
        for (const FCategorySummary& Entry : Summaries) {
            Summary.Append(TEXT(" - "));
            Entry.Category.AppendString(Summary);
            Summary.Appendf(TEXT(": %s (%d 项)\n"), Entry.bEnabled ? TEXT("ON") : TEXT("OFF"), Entry.ItemCount);
        }
        FGameDebugPrint::Print(FString(Summary.ToView()), FColor::Yellow, 6.0f);
        return;
    }

    if (Args.Num() < 2) {
        FGameDebugPrint::PrintWarning(TEXT("用法: GM.Watch list | GM.Watch all <on/off> | GM.Watch <Category> <on/off>"));
        return;
    }

    bool bEnable = false;
    if (!TryParseToggleFlag(Args[1], bEnable)) {
        FGameDebugPrint::PrintWarning(FString::Printf(TEXT("【调试观察中心】非法开关参数: %s（请使用 on/off/1/0/true/false）"), *Args[1]));
        GAME_LOG(Warning, TEXT("【GameDebug】【WatchHub】GM.Watch 收到非法开关参数: %s"), *Args[1]);
        return;
    }

    if (FirstArg.Equals(TEXT("all"), ESearchCase::IgnoreCase)) {
        SetGlobalEnabled(bEnable);
        FGameDebugPrint::Print(
            FString::Printf(TEXT("【调试观察中心】全局显示已%s"), bEnable ? TEXT("开启") : TEXT("关闭")),
            FColor::Green,
            3.0f
        );
        return;
    }

    if (FirstArg.IsEmpty()) {
        FGameDebugPrint::PrintWarning(TEXT("【调试观察中心】分类名称不可为空"));
        GAME_LOG(Warning, TEXT("【GameDebug】【WatchHub】GM.Watch 收到空分类名称。"));
        return;
    }

    const FName CatName(*Args[0]);
    if (CatName.IsNone()) {
        FGameDebugPrint::PrintWarning(TEXT("【调试观察中心】分类名称无效"));
        GAME_LOG(Warning, TEXT("【GameDebug】【WatchHub】GM.Watch 收到无效分类名称: %s"), *Args[0]);
        return;
    }

    SetCategoryEnabled(CatName, bEnable);
    FGameDebugPrint::Print(
        FString::Printf(TEXT("【调试观察中心】分类 [%s] 显示已%s"), *CatName.ToString(), bEnable ? TEXT("开启") : TEXT("关闭")),
        FColor::Green,
        3.0f
    );
}

FGameDebugWatchHub& FGameDebugWatchHub::Get() {
    static FGameDebugWatchHub Instance;
    return Instance;
}
