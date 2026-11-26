# GameAssetCore 快速上手与最佳实践指南

---

## 一、 接入模块依赖

在业务模块的 `*.Build.cs` 中添加 `"GameCore"` 与 `"GameAssetCore"`：

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameCore",
    "GameAssetCore",
});
```

包含单个公共门面头 `#include "GameAssetCore.h"`，或按需包含子域头文件（如 `#include "Loader/FGameAssetLoader.h"`）。

---

## 二、 典型 C++ 用法

### 1. 弱宿主保护异步加载资产与类
```cpp
#include "Loader/FGameAssetLoader.h"

void AMyWeapon::EquipSkinAsync(const TSoftObjectPtr<UStaticMesh>& SkinMesh, const TSoftClassPtr<AActor>& MuzzleFxClass) {
    // 1. 异步加载静态网格（若 AMyWeapon 在完成前销毁，回调自动丢弃绝不崩溃）
    SkinLoadHandle = FGameAssetLoader::LoadAsync<UStaticMesh>(
        SkinMesh,
        this,
        [this](UStaticMesh* LoadedMesh) {
            if (LoadedMesh != nullptr) {
                WeaponMeshComp->SetStaticMesh(LoadedMesh);
            }
        },
        FCancellationToken::None(),
        EGameAssetPriority::High
    );

    // 2. 异步加载受 AActor 基类契约保护的蓝图类
    FGameAssetLoader::LoadClassAsync<AActor>(
        MuzzleFxClass,
        this,
        [this](TSubclassOf<AActor> LoadedClass) {
            if (LoadedClass != nullptr) {
                CachedMuzzleClass = LoadedClass;
            }
        }
    );
}
```

### 2. 事件驱动 C++20 协程加载（单资产与批量并发）
```cpp
#include "Loader/FGameAssetLoader.h"

TGameCoroTask<void> UBossEncounterManager::PreloadPhaseAssetsCoro(
    TSoftClassPtr<AActor> BossClass,
    TArray<TSoftObjectPtr<UStaticMesh>> ArenaProps,
    FCancellationToken Token
) {
    co_await Game::Coro::BindContext(this, Token);

    // 无需 UWorld，即使宿主是 UGameInstanceSubsystem 或普通 UObject 亦事件驱动精确唤醒
    const TGameStatusResult<TSubclassOf<AActor>> BossClassResult = co_await FGameAssetLoader::LoadClassAsyncCoro(
        BossClass,
        this,
        Token,
        EGameAssetPriority::Critical
    );
    if (BossClassResult.IsFailure() || !BossClassResult.GetValue()) {
        co_return;
    }

    const TGameStatusResult<TArray<UStaticMesh*>> PropsResult = co_await FGameAssetLoader::LoadBatchAsyncCoro(
        MoveTemp(ArenaProps),
        this,
        Token,
        EGameAssetPriority::High
    );
    if (PropsResult.IsSuccess()) {
        for (UStaticMesh* PropMesh : PropsResult.GetValue()) {
            if (PropMesh != nullptr) {
                RegisterArenaMesh(PropMesh);
            }
        }
    }
}
```

### 3. RAII 作用域常驻保活与全局引用计数保活
```cpp
#include "Pinning/FScopedPinnedAsset.h"

void UCombatVfxDirector::PlayTransientComboVfx(const TSoftObjectPtr<UObject>& VfxAsset) {
    // 构造仅 1 次加锁，离开作用域自动递减引用计数；Get() 为栈缓存 O(1) 零锁访问
    FScopedPinnedAsset PinnedVfx(VfxAsset);
    if (UObject* AssetObj = PinnedVfx.Get()) {
        TriggerVfx(AssetObj);
    }
}
```

---

## 三、 性能与工程避坑守则

1. **避免主线程大资产同步硬加载**：`LoadSync` / `LoadClassSync` 仅适用于启动初始化或已知极小配置资产；运行期表现资产一律优先使用 `LoadAsync*` 或 `Load*Coro`。
2. **保存长期在途句柄并在组件销毁时取消**：虽然弱宿主守卫能保证宿主销毁后不误调回调，但在 `EndPlay` / `BeginDestroy` 主动调用 `Handle.Cancel()` 可提前通知引擎流送器释放带宽。
3. **成对管理手工常驻锁**：若直接调用 `FGameAssetPinner::PinAsset` 而非 `FScopedPinnedAsset`，务必在关卡退出或玩法结束时成对调用 `UnpinAsset`，避免无用资产长期占用内存预算。
