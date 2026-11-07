/**
 * @file FGameAssetLoader.h
 * @brief 弱引用守卫安全异步软引用资产加载器与内存保活原语
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "Asset/FGameAssetLoadHandle.h"
#include "Types/FCancellationToken.h"

/**
 * @brief 遵循 RAII 原则的资产内存保活作用域守卫（离开作用域自动解除保活）
 */
class GAMECORE_API FScopedPinnedAsset {
public:
    FScopedPinnedAsset() = default;

    /**
     * @brief 构造并立即尝试锁定目标资产路径
     * @param InPath 目标资产路径
     */
    explicit FScopedPinnedAsset(FSoftObjectPath InPath);

    /**
     * @brief 析构时若持有锁定则自动解除保活
     */
    ~FScopedPinnedAsset();

    FScopedPinnedAsset(const FScopedPinnedAsset&) = delete;
    FScopedPinnedAsset& operator=(const FScopedPinnedAsset&) = delete;

    /**
     * @brief 移动构造函数（接管保活路径并清空源对象）
     * @param Other 源保活守卫
     */
    FScopedPinnedAsset(FScopedPinnedAsset&& Other) noexcept;

    /**
     * @brief 移动赋值运算符（释放当前锁定并接管源对象锁定）
     * @param Other 源保活守卫
     * @return 自身引用
     */
    FScopedPinnedAsset& operator=(FScopedPinnedAsset&& Other) noexcept;

    /**
     * @brief 主动提前解除保活锁定
     */
    void Release() noexcept;

    /**
     * @brief 查询当前守卫是否持有有效的保活锁定
     * @return 持有锁定返回 true
     */
    [[nodiscard]] bool IsPinned() const noexcept;

    /**
     * @brief 获取当前保活的资产软路径
     * @return 资产软路径引用
     */
    [[nodiscard]] const FSoftObjectPath& GetPath() const noexcept;

private:
    FSoftObjectPath PinnedPath;
};

/**
 * @brief 宿主弱引用自动防悬垂的软引用资产异步加载纯静态工具类
 */
class GAMECORE_API FGameAssetLoader {
public:
    FGameAssetLoader() = delete;

public:
    /**
     * @brief 异步加载单个软引用资产（已驻留内存时同步短路回调，宿主失效时静默丢弃回调）
     * @tparam TAsset 目标 UObject 派生资产类型
     * @param SoftAsset 目标资产软引用
     * @param Owner 宿主对象弱引用（入队前或完成时若失效则自动短路不回调）
     * @param OnLoaded 加载结果回调（解析成功传入资产指针，路径不存在或类型不匹配传入 nullptr）
     * @param Priority 流式加载优先级
     * @return 可取消的加载控制句柄（同步短路时返回空句柄）
     */
    template <typename TAsset>
    static FGameAssetLoadHandle LoadAsync(
        const TSoftObjectPtr<TAsset>& SoftAsset,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void(TAsset*)> OnLoaded,
        TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority
    );

    /**
     * @brief 异步加载软引用类（已驻留内存时同步短路回调，宿主失效时静默丢弃回调）
     * @tparam TClass 目标类基类类型
     * @param SoftClass 目标类软引用
     * @param Owner 宿主对象弱引用（入队前或完成时若失效则自动短路不回调）
     * @param OnLoaded 加载结果回调（解析成功传入 UClass 指针，路径不存在或类型不匹配传入 nullptr）
     * @param Priority 流式加载优先级
     * @return 可取消的加载控制句柄（同步短路时返回空句柄）
     */
    template <typename TClass>
    static FGameAssetLoadHandle LoadClassAsync(
        const TSoftClassPtr<TClass>& SoftClass,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void(UClass*)> OnLoaded,
        TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority
    );

    /**
     * @brief 异步批量加载一组软对象路径并在全部完成且宿主仍有效时触发通知
     * @param AssetPaths 待加载软对象路径数组（自动过滤空路径；过滤后为空时若宿主有效立即触发回调）
     * @param Owner 宿主对象弱引用
     * @param OnAllLoaded 批次完成回调
     * @param Priority 流式加载优先级
     * @return 可取消的加载控制句柄
     */
    static FGameAssetLoadHandle LoadBatchAsync(
        const TArray<FSoftObjectPath>& AssetPaths,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void()> OnAllLoaded,
        TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority
    );

    /**
     * @brief 异步批量加载一组软对象路径并在全部完成且宿主仍有效时触发通知（支持综合进度汇报与取消令牌联动）
     * @param AssetPaths 待加载软对象路径数组（自动过滤空路径）
     * @param Owner 宿主对象弱引用
     * @param OnAllLoaded 批次完成回调
     * @param OnProgress 批次进度更新回调（参数为 0.0f ~ 1.0f 浮点比例）
     * @param CancellationToken 协同取消令牌（当收到取消请求时自动解除异步回调）
     * @param Priority 流式加载优先级
     * @return 可取消的加载控制句柄
     */
    static FGameAssetLoadHandle LoadBatchAsync(
        const TArray<FSoftObjectPath>& AssetPaths,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void()> OnAllLoaded,
        TFunction<void(float Progress)> OnProgress,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        TAsyncLoadPriority Priority = FStreamableManager::DefaultAsyncLoadPriority
    );

    /**
     * @brief 同步阻塞加载单个软引用资产（若已驻留内存则直接返回）
     * @tparam TAsset 目标 UObject 派生资产类型
     * @param SoftAsset 目标资产软引用
     * @return 成功加载的资产裸指针，失败或软引用为空返回 nullptr
     */
    template <typename TAsset>
    static TAsset* LoadSync(const TSoftObjectPtr<TAsset>& SoftAsset);

    /**
     * @brief 同步阻塞加载软引用类（若已驻留内存则直接返回）
     * @tparam TClass 目标类基类类型
     * @param SoftClass 目标类软引用
     * @return 成功加载的 UClass 指针，失败或软引用为空返回 nullptr
     */
    template <typename TClass>
    static UClass* LoadClassSync(const TSoftClassPtr<TClass>& SoftClass);

    /**
     * @brief 锁定资产路径在内存中的常驻状态（内部增加引用计数，防止未挂载被 GC 误杀回收）
     * @param AssetPath 待锁定的资产软路径
     * @return 锁定成功（路径有效且成功加载与持有句柄）返回 true
     */
    static bool PinAsset(const FSoftObjectPath& AssetPath);

    /**
     * @brief 解除资产路径的内存常驻锁定（内部递减引用计数，计数归零时释放句柄）
     * @param AssetPath 待解除锁定的资产软路径
     * @return 成功扣减或解除返回 true，路径未被锁定返回 false
     */
    static bool UnpinAsset(const FSoftObjectPath& AssetPath);

    /**
     * @brief 检查指定资产路径当前是否处于保活锁定状态
     * @param AssetPath 待查询的资产软路径
     * @return 处于保活状态返回 true
     */
    [[nodiscard]] static bool IsAssetPinned(const FSoftObjectPath& AssetPath);

    /**
     * @brief 清空全部资产的保活锁定引用（主要用于测试复位或关卡全局重置）
     */
    static void ClearAllPinnedAssets();
};

template <typename TAsset>
FGameAssetLoadHandle FGameAssetLoader::LoadAsync(
    const TSoftObjectPtr<TAsset>& SoftAsset,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(TAsset*)> OnLoaded,
    TAsyncLoadPriority Priority
) {
    if (!Owner.IsValid() || !OnLoaded || SoftAsset.IsNull()) {
        return FGameAssetLoadHandle();
    }

    if (TAsset* ResidentAsset = SoftAsset.Get()) {
        OnLoaded(ResidentAsset);
        return FGameAssetLoadHandle();
    }

    const FSoftObjectPath TargetPath = SoftAsset.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        TargetPath,
        FStreamableDelegate::CreateLambda([Owner, TargetPath, Callback = MoveTemp(OnLoaded)]() mutable {
            if (!Owner.IsValid()) {
                return;
            }
            Callback(Cast<TAsset>(TargetPath.ResolveObject()));
        }),
        Priority
    );

    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}

template <typename TClass>
FGameAssetLoadHandle FGameAssetLoader::LoadClassAsync(
    const TSoftClassPtr<TClass>& SoftClass,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(UClass*)> OnLoaded,
    TAsyncLoadPriority Priority
) {
    if (!Owner.IsValid() || !OnLoaded || SoftClass.IsNull()) {
        return FGameAssetLoadHandle();
    }

    if (UClass* ResidentClass = SoftClass.Get()) {
        OnLoaded(ResidentClass);
        return FGameAssetLoadHandle();
    }

    const FSoftObjectPath TargetPath = SoftClass.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestAsyncLoad(
        TargetPath,
        FStreamableDelegate::CreateLambda([Owner, TargetPath, Callback = MoveTemp(OnLoaded)]() mutable {
            if (!Owner.IsValid()) {
                return;
            }
            Callback(Cast<UClass>(TargetPath.ResolveObject()));
        }),
        Priority
    );

    return FGameAssetLoadHandle(MoveTemp(RawHandle));
}

template <typename TAsset>
TAsset* FGameAssetLoader::LoadSync(const TSoftObjectPtr<TAsset>& SoftAsset) {
    if (SoftAsset.IsNull()) {
        return nullptr;
    }

    if (TAsset* Resident = SoftAsset.Get()) {
        return Resident;
    }

    const FSoftObjectPath TargetPath = SoftAsset.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    const TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestSyncLoad(TargetPath);
    return RawHandle.IsValid() ? Cast<TAsset>(RawHandle->GetLoadedAsset()) : Cast<TAsset>(TargetPath.ResolveObject());
}

template <typename TClass>
UClass* FGameAssetLoader::LoadClassSync(const TSoftClassPtr<TClass>& SoftClass) {
    if (SoftClass.IsNull()) {
        return nullptr;
    }

    if (UClass* Resident = SoftClass.Get()) {
        return Resident;
    }

    const FSoftObjectPath TargetPath = SoftClass.ToSoftObjectPath();
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    const TSharedPtr<FStreamableHandle> RawHandle = Streamable.RequestSyncLoad(TargetPath);
    return RawHandle.IsValid() ? Cast<UClass>(RawHandle->GetLoadedAsset()) : Cast<UClass>(TargetPath.ResolveObject());
}
