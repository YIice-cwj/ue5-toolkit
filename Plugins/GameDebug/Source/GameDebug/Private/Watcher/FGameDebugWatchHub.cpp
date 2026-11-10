/**
 * @file FGameDebugWatchHub.cpp
 * @brief 现代化通用运行时调试观察中心实现
 */

#include "Watcher/FGameDebugWatchHub.h"

#include "Cheat/FGameCheatRegistry.h"
#include "Print/FGameDebugPrint.h"

FGameDebugWatchHub::FGameDebugWatchHub() {
#if !UE_BUILD_SHIPPING
    FGameCheatRegistry::Get().RegisterCheat(
        FName(TEXT("GM.Watch")),
        TEXT("控制调试观察中心显示状态。用法: GM.Watch list | GM.Watch all <on/off> | GM.Watch <Category> <on/off>"),
        [this](const TArray<FString>& Args) {
            if (Args.IsEmpty()) {
                FGameDebugPrint::PrintWarning(TEXT("用法: GM.Watch list | GM.Watch all <on/off> | GM.Watch <Category> <on/off>"));
                return;
            }

            const FString Action = Args[0].ToLower();
            if (Action == TEXT("list")) {
                TArray<FName> CatNames;
                GetCategories(CatNames);
                FString Summary = FString::Printf(TEXT("【调试观察中心】当前分类总数: %d\n"), CatNames.Num());
                for (const FName& Cat : CatNames) {
                    Summary += FString::Printf(TEXT(" - %s: %s\n"), *Cat.ToString(), IsCategoryEnabled(Cat) ? TEXT("ON") : TEXT("OFF"));
                }
                FGameDebugPrint::Print(Summary, FColor::Yellow, 6.0f);
                return;
            }

            if (Action == TEXT("all") && Args.Num() >= 2) {
                const bool bEnable = Args[1].ToLower() == TEXT("on") || Args[1] == TEXT("1");
                SetGlobalEnabled(bEnable);
                FGameDebugPrint::Print(FString::Printf(TEXT("【调试观察中心】全局显示已%s"), bEnable ? TEXT("开启") : TEXT("关闭")), FColor::Green, 3.0f);
                return;
            }

            if (Args.Num() >= 2) {
                const FName CatName(*Args[0]);
                const bool bEnable = Args[1].ToLower() == TEXT("on") || Args[1] == TEXT("1");
                SetCategoryEnabled(CatName, bEnable);
                FGameDebugPrint::Print(FString::Printf(TEXT("【调试观察中心】分类 [%s] 显示已%s"), *CatName.ToString(), bEnable ? TEXT("开启") : TEXT("关闭")), FColor::Green, 3.0f);
            }
        }
    );
#endif
}

FGameDebugWatchHub::~FGameDebugWatchHub() {
    ClearAll();
}

FGameDebugWatchHub& FGameDebugWatchHub::Get() {
    static FGameDebugWatchHub Instance;
    return Instance;
}

void FGameDebugWatchHub::Watch(const FName& Category, const FName& Name, FValueGetter Getter) {
    if (Category.IsNone() || Name.IsNone() || !Getter) {
        return;
    }

    FWriteScopeLock WriteLock(RegistryLock);
    FCategoryGroup& Group = Categories.FindOrAdd(Category);
    Group.Category = Category;

    for (FWatchItem& Item : Group.Items) {
        if (Item.Name == Name) {
            Item.Getter = MoveTemp(Getter);
            return;
        }
    }

    Group.Items.Add(FWatchItem{Name, MoveTemp(Getter)});
}

bool FGameDebugWatchHub::Unwatch(const FName& Category, const FName& Name) {
    if (Category.IsNone() || Name.IsNone()) {
        return false;
    }

    FWriteScopeLock WriteLock(RegistryLock);
    if (FCategoryGroup* Group = Categories.Find(Category)) {
        const int32 Removed = Group->Items.RemoveAll([&Name](const FWatchItem& Item) {
            return Item.Name == Name;
        });
        return Removed > 0;
    }
    return false;
}

void FGameDebugWatchHub::SetCategoryEnabled(const FName& Category, bool bEnabled) {
    if (Category.IsNone()) {
        return;
    }

    FWriteScopeLock WriteLock(RegistryLock);
    FCategoryGroup& Group = Categories.FindOrAdd(Category);
    Group.Category = Category;
    Group.bEnabled = bEnabled;
}

bool FGameDebugWatchHub::IsCategoryEnabled(const FName& Category) const {
    if (Category.IsNone()) {
        return false;
    }

    FReadScopeLock ReadLock(RegistryLock);
    if (const FCategoryGroup* Group = Categories.Find(Category)) {
        return Group->bEnabled;
    }
    return false;
}

void FGameDebugWatchHub::SetGlobalEnabled(bool bEnabled) noexcept {
    bGlobalEnabled = bEnabled;
}

bool FGameDebugWatchHub::IsGlobalEnabled() const noexcept {
    return bGlobalEnabled;
}

void FGameDebugWatchHub::GetCategories(TArray<FName>& OutCategories) const {
    FReadScopeLock ReadLock(RegistryLock);
    Categories.GetKeys(OutCategories);
}

void FGameDebugWatchHub::ClearAll() {
    FWriteScopeLock WriteLock(RegistryLock);
    Categories.Empty();
}

void FGameDebugWatchHub::TickDisplay([[maybe_unused]] float DeltaTime) {
#if !UE_BUILD_SHIPPING
    if (!bGlobalEnabled) {
        return;
    }

    struct FCategorySnapshot {
        FName Category;
        TArray<FWatchItem> Items;
    };

    TArray<FCategorySnapshot> ActiveSnapshots;
    {
        FReadScopeLock ReadLock(RegistryLock);
        ActiveSnapshots.Reserve(Categories.Num());
        for (const TPair<FName, FCategoryGroup>& Pair : Categories) {
            if (Pair.Value.bEnabled && !Pair.Value.Items.IsEmpty()) {
                ActiveSnapshots.Add(FCategorySnapshot{Pair.Key, Pair.Value.Items});
            }
        }
    }

    for (const FCategorySnapshot& Snapshot : ActiveSnapshots) {
        FString DisplayBuffer;
        DisplayBuffer.Reserve(256);
        DisplayBuffer.Appendf(TEXT("=== [ %s ] ===\n"), *Snapshot.Category.ToString());

        for (const FWatchItem& Item : Snapshot.Items) {
            FString ValueString;
            if (Item.Getter) {
                ValueString = Item.Getter();
            }
            DisplayBuffer.Appendf(TEXT("  %-16s: %s\n"), *Item.Name.ToString(), *ValueString);
        }

        const uint64 CategoryScreenKey = static_cast<uint64>(GetTypeHash(Snapshot.Category));
        FGameDebugPrint::Print(DisplayBuffer, FColor::Cyan, 0.1f, CategoryScreenKey);
    }
#endif
}
