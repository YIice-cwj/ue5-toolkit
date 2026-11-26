/**
 * @file FGameAssetLoader.h
 * @brief 弱宿主守卫异步软引用资产加载器与事件驱动 C++20 协程加载门面声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Templates/Casts.h"
#include "Templates/SubclassOf.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include "Async/FCancellationToken.h"
#include "Async/FGameCoroAwaiters.h"
#include "Async/TGameCoroTask.h"
#include "Core/AssetErrorCodes.h"
#include "Core/GameAssetTypes.h"
#include "Loader/FGameAssetLoadHandle.h"
#include "Types/TGameResult.h"

class UAsyncAction_LoadGameAsset;
class UAsyncAction_LoadGameAssetsBatch;

/**
 * @brief 宿主弱引用防悬垂的软引用资产异步流式、事件驱动协程与同步加载静态门面
 */
class GAMEASSETCORE_API FGameAssetLoader {
private:
    friend class UAsyncAction_LoadGameAsset;
    friend class UAsyncAction_LoadGameAssetsBatch;

    struct FSharedCoroLoadState;

    /**
     * @brief 单资产事件驱动 C++20 流式加载等待器（零帧轮询、零 UWorld 子系统依赖）
     */
    struct FStreamableAssetAwaiter {
        FSoftObjectPath TargetPath;
        FCancellationToken Token;
        EGameAssetPriority Priority = EGameAssetPriority::Normal;
        UObject* PreResolvedObject = nullptr;
        TSharedPtr<FSharedCoroLoadState, ESPMode::ThreadSafe> SharedState;

        /**
         * @brief 构造单资产事件驱动流式加载等待器
         * @param InPath 目标资产软路径
         * @param InToken 协同取消令牌
         * @param InPriority 业务加载优先级
         */
        FStreamableAssetAwaiter(
            FSoftObjectPath InPath,
            FCancellationToken InToken,
            EGameAssetPriority InPriority
        );

        /**
         * @brief 移动构造函数
         * @param Other 源等待器实例
         */
        FStreamableAssetAwaiter(FStreamableAssetAwaiter&& Other) noexcept;

        /**
         * @brief 析构函数
         */
        ~FStreamableAssetAwaiter();

        FStreamableAssetAwaiter(const FStreamableAssetAwaiter&) = delete;
        FStreamableAssetAwaiter& operator=(const FStreamableAssetAwaiter&) = delete;
        FStreamableAssetAwaiter& operator=(FStreamableAssetAwaiter&&) = delete;

        /**
         * @brief 检查目标路径是否无效或已驻留内存以决定是否免挂起直返
         * @return 无需挂起返回 true
         */
        [[nodiscard]] bool await_ready();

        /**
         * @brief 挂起协程并向 FStreamableManager 注册单次唤醒委托与跨线程安全取消通道
         * @param InGuard 挂起点协程帧安全守卫
         */
        void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard);

        /**
         * @brief 协程恢复后提取已加载的有效 UObject 指针
         * @return 加载成功返回资产指针，失败返回 nullptr
         */
        [[nodiscard]] UObject* await_resume();
    };

    /**
     * @brief 批量路径事件驱动 C++20 流式加载等待器（零帧轮询、全程 GC 保活、取消即时中止底层句柄）
     */
    struct FStreamableBatchAwaiter {
        TArray<FSoftObjectPath> UniquePaths;
        FCancellationToken Token;
        EGameAssetPriority Priority = EGameAssetPriority::Normal;
        TSharedPtr<FSharedCoroLoadState, ESPMode::ThreadSafe> SharedState;

        /**
         * @brief 构造批量路径事件驱动流式加载等待器
         * @param InUniquePaths 去重后的有效软对象路径列表
         * @param InToken 协同取消令牌
         * @param InPriority 业务加载优先级
         */
        FStreamableBatchAwaiter(
            TArray<FSoftObjectPath> InUniquePaths,
            FCancellationToken InToken,
            EGameAssetPriority InPriority
        );

        /**
         * @brief 移动构造函数
         * @param Other 源等待器实例
         */
        FStreamableBatchAwaiter(FStreamableBatchAwaiter&& Other) noexcept;

        /**
         * @brief 析构函数
         */
        ~FStreamableBatchAwaiter();

        FStreamableBatchAwaiter(const FStreamableBatchAwaiter&) = delete;
        FStreamableBatchAwaiter& operator=(const FStreamableBatchAwaiter&) = delete;
        FStreamableBatchAwaiter& operator=(FStreamableBatchAwaiter&&) = delete;

        /**
         * @brief 若去重路径数组为空则无需挂起直返
         * @return 无需挂起返回 true
         */
        [[nodiscard]] bool await_ready() const noexcept;

        /**
         * @brief 挂起协程并向 FStreamableManager 注册批量流送单次唤醒委托与跨线程安全取消通道
         * @param InGuard 挂起点协程帧安全守卫
         */
        void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard);

        /**
         * @brief 协程恢复后交出底层流式保活句柄，确保调用方结果解析窗口内批次资产受 GC 保护
         * @return 持有当前批次引用的加载控制句柄
         */
        [[nodiscard]] FGameAssetLoadHandle await_resume();
    };

public:
    FGameAssetLoader() = delete;

    /**
     * @brief 异步加载单个强类型软引用资产（已驻留内存时同步短路回调，宿主失效或取消时自动拦截）
     * @tparam TAsset 目标 UObject 派生资产类型
     * @param SoftAsset 目标资产软引用
     * @param Owner 宿主对象弱引用（入队前或完成时若已销毁则自动丢弃回调）
     * @param OnLoaded 加载结果回调（成功传入强类型资产指针，路径不存在或类型不兼容传入 nullptr）
     * @param CancellationToken 协同取消令牌（默认 None）
     * @param Priority 业务语义加载优先级（默认 Normal）
     * @return 可取消的加载控制句柄（同步短路或前置拦截时返回空句柄）
     */
    template <typename TAsset>
    static FGameAssetLoadHandle LoadAsync(
        const TSoftObjectPtr<TAsset>& SoftAsset,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void(TAsset*)> OnLoaded,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 异步加载强类型软引用类（强制校验 IsChildOf<TClass> 继承契约，已驻留时同步短路）
     * @tparam TClass 目标类基类契约类型
     * @param SoftClass 目标类软引用
     * @param Owner 宿主对象弱引用（入队前或完成时若已销毁则自动丢弃回调）
     * @param OnLoaded 加载结果回调（成功传入受 TClass 约束的 TSubclassOf<TClass>，不存在或基类不兼容传入 nullptr）
     * @param CancellationToken 协同取消令牌（默认 None）
     * @param Priority 业务语义加载优先级（默认 Normal）
     * @return 可取消的加载控制句柄（同步短路或前置拦截时返回空句柄）
     */
    template <typename TClass>
    static FGameAssetLoadHandle LoadClassAsync(
        const TSoftClassPtr<TClass>& SoftClass,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void(TSubclassOf<TClass>)> OnLoaded,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 基于事件驱动 C++20 协程的软引用资产异步加载原语（按值传参杜绝跨挂起悬垂，驻留零挂起）
     * @tparam TAsset 目标 UObject 派生资产类型
     * @param SoftAsset 目标资产软引用（按值存入协程帧）
     * @param Owner 关联存活守卫的宿主 UObject（宿主销毁时自动中止并销毁挂起协程帧）
     * @param CancellationToken 协同取消令牌（按值存入协程帧，默认 None）
     * @param Priority 业务语义加载优先级（默认 Normal）
     * @return 协程任务，成功返回强类型资产指针，取消、路径不存在或类型不兼容返回 nullptr
     */
    template <typename TAsset>
    static TGameCoroTask<TAsset*> LoadAsyncCoro(
        TSoftObjectPtr<TAsset> SoftAsset,
        UObject* Owner,
        FCancellationToken CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 基于事件驱动 C++20 协程的软引用类异步加载原语（按值传参杜绝悬垂，强制校验 IsChildOf<TClass>）
     * @tparam TClass 目标类基类契约类型
     * @param SoftClass 目标类软引用（按值存入协程帧）
     * @param Owner 关联存活守卫的宿主 UObject（宿主销毁时自动中止并销毁挂起协程帧）
     * @param CancellationToken 协同取消令牌（按值存入协程帧，默认 None）
     * @param Priority 业务语义加载优先级（默认 Normal）
     * @return 协程任务，成功返回 TSubclassOf<TClass>，取消、路径不存在或基类不兼容返回 nullptr
     */
    template <typename TClass>
    static TGameCoroTask<TSubclassOf<TClass>> LoadClassAsyncCoro(
        TSoftClassPtr<TClass> SoftClass,
        UObject* Owner,
        FCancellationToken CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 基于事件驱动 C++20 协程的软引用资产批量异步加载原语（按值传参、全驻留零挂起、异步全程 GC 保活）
     * @tparam TAsset 目标 UObject 派生资产类型（默认 UObject）
     * @param SoftAssets 待加载的软引用资产数组（按值存入协程帧，安全支持临时列表入参）
     * @param Owner 关联存活守卫的宿主 UObject
     * @param CancellationToken 协同取消令牌（按值存入协程帧，默认 None）
     * @param Priority 业务语义加载优先级（默认 Normal）
     * @return 协程任务，返回与输入等长且顺序 1:1 对齐的已解析资产数组（空项、失败项或类型不兼容项对应位置为 nullptr）
     */
    template <typename TAsset = UObject>
    static TGameCoroTask<TArray<TAsset*>> LoadBatchAsyncCoro(
        TArray<TSoftObjectPtr<TAsset>> SoftAssets,
        UObject* Owner,
        FCancellationToken CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 异步批量预加载一组软对象路径并在全部完成且宿主仍有效时触发通知（内部 O(N) 去重，支持进度与取消联动）
     * @param AssetPaths 待加载软对象路径数组（自动忽略无效空路径，全驻留时零分配短路）
     * @param Owner 宿主对象弱引用
     * @param OnAllLoaded 批次完成回调
     * @param OnProgress 批次进度更新回调（[0.0f, 1.0f]，仅成功完成时上报 1.0f，默认空）
     * @param CancellationToken 协同取消令牌（默认 None）
     * @param Priority 业务语义加载优先级（默认 Normal）
     * @return 可取消的加载控制句柄
     */
    static FGameAssetLoadHandle LoadBatchAsync(
        const TArray<FSoftObjectPath>& AssetPaths,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void()> OnAllLoaded,
        TFunction<void(float Progress)> OnProgress = nullptr,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 异步批量加载一组强类型软引用资产并在完成时交付按原顺序对齐的强类型指针数组
     * @tparam TAsset 目标 UObject 派生资产类型（默认 UObject）
     * @param SoftAssets 待加载软引用资产数组
     * @param Owner 宿主对象弱引用
     * @param OnAllLoaded 批次完成回调（接收与 SoftAssets 等长对齐的 const TArray<TAsset*>&）
     * @param OnProgress 批次进度更新回调（[0.0f, 1.0f]，仅成功完成时上报 1.0f，默认空）
     * @param CancellationToken 协同取消令牌（默认 None）
     * @param Priority 业务语义加载优先级（默认 Normal）
     * @return 可取消的加载控制句柄
     */
    template <typename TAsset = UObject>
    static FGameAssetLoadHandle LoadBatchAsync(
        const TArray<TSoftObjectPtr<TAsset>>& SoftAssets,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void(const TArray<TAsset*>&)> OnAllLoaded,
        TFunction<void(float Progress)> OnProgress = nullptr,
        const FCancellationToken& CancellationToken = FCancellationToken::None(),
        EGameAssetPriority Priority = EGameAssetPriority::Normal
    );

    /**
     * @brief 同步阻塞加载单个软引用资产（已驻留内存时零流式开销直返，严格区分加载失败与类型不匹配错误码）
     * @tparam TAsset 目标 UObject 派生资产类型
     * @param SoftAsset 目标资产软引用
     * @return 成功返回强类型资产指针，失败返回 InvalidAssetPath / InvalidThread / LoadFailed / TypeMismatch 错误
     */
    template <typename TAsset>
    static TGameStatusResult<TAsset*> LoadSync(const TSoftObjectPtr<TAsset>& SoftAsset);

    /**
     * @brief 同步阻塞加载软引用类（强制校验 IsChildOf<TClass>，严格区分加载失败与基类不兼容错误码）
     * @tparam TClass 目标类基类契约类型
     * @param SoftClass 目标类软引用
     * @return 成功返回 TSubclassOf<TClass>，失败返回 InvalidAssetPath / InvalidThread / LoadFailed / TypeMismatch 错误
     */
    template <typename TClass>
    static TGameStatusResult<TSubclassOf<TClass>> LoadClassSync(const TSoftClassPtr<TClass>& SoftClass);

private:
    static FGameAssetLoadHandle LoadRawAsync(
        const FSoftObjectPath& TargetPath,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void(UObject*)> OnLoaded,
        TFunction<void(float Progress)> OnProgress,
        const FCancellationToken& CancellationToken,
        EGameAssetPriority Priority
    );

    static FGameAssetLoadHandle LoadRawBatchAsync(
        TConstArrayView<FSoftObjectPath> AssetPaths,
        TWeakObjectPtr<UObject> Owner,
        TFunction<void()> OnAllLoaded,
        TFunction<void(float Progress)> OnProgress,
        const FCancellationToken& CancellationToken,
        EGameAssetPriority Priority
    );

    static TGameStatusResult<UObject*> LoadRawSync(const FSoftObjectPath& TargetPath);

    [[nodiscard]] static UObject* ResolveResidentObject(const FSoftObjectPath& TargetPath);

    static TArray<FSoftObjectPath> CollectUniqueValidPaths(TConstArrayView<FSoftObjectPath> InPaths);

    static void LogAssetTypeMismatch(
        const FSoftObjectPath& TargetPath,
        const UClass* ActualClass,
        const UClass* ExpectedClass
    );

    template <typename TAsset>
    [[nodiscard]] static TAsset* CastCheckedAssetOrLog(
        const FSoftObjectPath& TargetPath,
        UObject* RawObject
    ) {
        UObject* ValidObject = ::GetValid(RawObject);
        if (ValidObject == nullptr) {
            return nullptr;
        }
        TAsset* TypedAsset = Cast<TAsset>(ValidObject);
        if (TypedAsset == nullptr) {
            LogAssetTypeMismatch(TargetPath, ValidObject->GetClass(), TAsset::StaticClass());
        }
        return TypedAsset;
    }

    template <typename TClass>
    [[nodiscard]] static TSubclassOf<TClass> CastCheckedClassOrLog(
        const FSoftObjectPath& TargetPath,
        UObject* RawObject
    ) {
        UObject* ValidObject = ::GetValid(RawObject);
        if (ValidObject == nullptr) {
            return nullptr;
        }
        UClass* LoadedClass = Cast<UClass>(ValidObject);
        if (LoadedClass != nullptr && LoadedClass->IsChildOf(TClass::StaticClass())) {
            return TSubclassOf<TClass>(LoadedClass);
        }
        LogAssetTypeMismatch(TargetPath, LoadedClass ? LoadedClass : ValidObject->GetClass(), TClass::StaticClass());
        return nullptr;
    }

    template <typename TAsset>
    [[nodiscard]] static bool AreAllBatchAssetsResident(const TArray<TSoftObjectPtr<TAsset>>& SoftAssets) {
        for (const TSoftObjectPtr<TAsset>& SoftAsset : SoftAssets) {
            if (SoftAsset.IsNull() || SoftAsset.Get() != nullptr) {
                continue;
            }
            if (ResolveResidentObject(SoftAsset.ToSoftObjectPath()) == nullptr) {
                return false;
            }
        }
        return true;
    }

    template <typename TAsset>
    [[nodiscard]] static TArray<FSoftObjectPath> CollectAllValidPaths(const TArray<TSoftObjectPtr<TAsset>>& SoftAssets) {
        TArray<FSoftObjectPath> ValidPaths;
        ValidPaths.Reserve(SoftAssets.Num());
        for (const TSoftObjectPtr<TAsset>& SoftAsset : SoftAssets) {
            if (!SoftAsset.IsNull()) {
                ValidPaths.Add(SoftAsset.ToSoftObjectPath());
            }
        }
        return ValidPaths;
    }

    template <typename TAsset>
    [[nodiscard]] static TArray<TAsset*> ResolveTypedBatchAssets(const TArray<TSoftObjectPtr<TAsset>>& SoftAssets) {
        TArray<TAsset*> Results;
        Results.Reserve(SoftAssets.Num());
        for (const TSoftObjectPtr<TAsset>& SoftAsset : SoftAssets) {
            if (SoftAsset.IsNull()) {
                Results.Add(nullptr);
                continue;
            }
            if (TAsset* Direct = SoftAsset.Get()) {
                Results.Add(Direct);
                continue;
            }
            const FSoftObjectPath Path = SoftAsset.ToSoftObjectPath();
            Results.Add(CastCheckedAssetOrLog<TAsset>(Path, ResolveResidentObject(Path)));
        }
        return Results;
    }
};

template <typename TAsset>
FGameAssetLoadHandle FGameAssetLoader::LoadAsync(
    const TSoftObjectPtr<TAsset>& SoftAsset,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(TAsset*)> OnLoaded,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    if (!Owner.IsValid() || !OnLoaded || SoftAsset.IsNull() || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    if (TAsset* ResidentAsset = SoftAsset.Get()) {
        OnLoaded(ResidentAsset);
        return FGameAssetLoadHandle();
    }

    const FSoftObjectPath TargetPath = SoftAsset.ToSoftObjectPath();
    if (UObject* MismatchedResident = ResolveResidentObject(TargetPath)) {
        OnLoaded(CastCheckedAssetOrLog<TAsset>(TargetPath, MismatchedResident));
        return FGameAssetLoadHandle();
    }

    return LoadRawAsync(
        TargetPath,
        Owner,
        [TargetPath, TypedCallback = MoveTemp(OnLoaded)](UObject* LoadedObject) mutable {
            TypedCallback(CastCheckedAssetOrLog<TAsset>(TargetPath, LoadedObject));
        },
        nullptr,
        CancellationToken,
        Priority
    );
}

template <typename TClass>
FGameAssetLoadHandle FGameAssetLoader::LoadClassAsync(
    const TSoftClassPtr<TClass>& SoftClass,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(TSubclassOf<TClass>)> OnLoaded,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    if (!Owner.IsValid() || !OnLoaded || SoftClass.IsNull() || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    if (UClass* ResidentClass = SoftClass.Get()) {
        OnLoaded(TSubclassOf<TClass>(ResidentClass));
        return FGameAssetLoadHandle();
    }

    const FSoftObjectPath TargetPath = SoftClass.ToSoftObjectPath();
    if (UObject* MismatchedResident = ResolveResidentObject(TargetPath)) {
        OnLoaded(CastCheckedClassOrLog<TClass>(TargetPath, MismatchedResident));
        return FGameAssetLoadHandle();
    }

    return LoadRawAsync(
        TargetPath,
        Owner,
        [TargetPath, TypedCallback = MoveTemp(OnLoaded)](UObject* LoadedObject) mutable {
            TypedCallback(CastCheckedClassOrLog<TClass>(TargetPath, LoadedObject));
        },
        nullptr,
        CancellationToken,
        Priority
    );
}

template <typename TAsset>
TGameCoroTask<TAsset*> FGameAssetLoader::LoadAsyncCoro(
    TSoftObjectPtr<TAsset> SoftAsset,
    UObject* Owner,
    FCancellationToken CancellationToken,
    EGameAssetPriority Priority
) {
    co_await Game::Coro::BindContext(Owner, CancellationToken);
    if (SoftAsset.IsNull()) {
        co_return nullptr;
    }

    if (TAsset* Resident = SoftAsset.Get()) {
        co_return Resident;
    }

    const FSoftObjectPath TargetPath = SoftAsset.ToSoftObjectPath();
    if (UObject* MismatchedResident = ResolveResidentObject(TargetPath)) {
        co_return CastCheckedAssetOrLog<TAsset>(TargetPath, MismatchedResident);
    }

    UObject* LoadedObject = co_await FStreamableAssetAwaiter(TargetPath, CancellationToken, Priority);
    co_return CastCheckedAssetOrLog<TAsset>(TargetPath, LoadedObject);
}

template <typename TClass>
TGameCoroTask<TSubclassOf<TClass>> FGameAssetLoader::LoadClassAsyncCoro(
    TSoftClassPtr<TClass> SoftClass,
    UObject* Owner,
    FCancellationToken CancellationToken,
    EGameAssetPriority Priority
) {
    co_await Game::Coro::BindContext(Owner, CancellationToken);
    if (SoftClass.IsNull()) {
        co_return nullptr;
    }

    if (UClass* ResidentClass = SoftClass.Get()) {
        co_return TSubclassOf<TClass>(ResidentClass);
    }

    const FSoftObjectPath TargetPath = SoftClass.ToSoftObjectPath();
    if (UObject* MismatchedResident = ResolveResidentObject(TargetPath)) {
        co_return CastCheckedClassOrLog<TClass>(TargetPath, MismatchedResident);
    }

    UObject* LoadedObject = co_await FStreamableAssetAwaiter(TargetPath, CancellationToken, Priority);
    co_return CastCheckedClassOrLog<TClass>(TargetPath, LoadedObject);
}

template <typename TAsset>
TGameCoroTask<TArray<TAsset*>> FGameAssetLoader::LoadBatchAsyncCoro(
    TArray<TSoftObjectPtr<TAsset>> SoftAssets,
    UObject* Owner,
    FCancellationToken CancellationToken,
    EGameAssetPriority Priority
) {
    co_await Game::Coro::BindContext(Owner, CancellationToken);
    if (SoftAssets.IsEmpty()) {
        co_return TArray<TAsset*>();
    }

    if (AreAllBatchAssetsResident(SoftAssets)) {
        co_return ResolveTypedBatchAssets(SoftAssets);
    }

    TArray<FSoftObjectPath> UniquePaths = CollectUniqueValidPaths(CollectAllValidPaths(SoftAssets));
    const FGameAssetLoadHandle BatchKeepAlive =
        co_await FStreamableBatchAwaiter(MoveTemp(UniquePaths), CancellationToken, Priority);
    co_return ResolveTypedBatchAssets(SoftAssets);
}

template <typename TAsset>
FGameAssetLoadHandle FGameAssetLoader::LoadBatchAsync(
    const TArray<TSoftObjectPtr<TAsset>>& SoftAssets,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(const TArray<TAsset*>&)> OnAllLoaded,
    TFunction<void(float Progress)> OnProgress,
    const FCancellationToken& CancellationToken,
    EGameAssetPriority Priority
) {
    if (!Owner.IsValid() || !OnAllLoaded || CancellationToken.IsCancellationRequested()) {
        return FGameAssetLoadHandle();
    }

    if (AreAllBatchAssetsResident(SoftAssets)) {
        if (OnProgress) {
            OnProgress(1.0f);
        }
        OnAllLoaded(ResolveTypedBatchAssets(SoftAssets));
        return FGameAssetLoadHandle();
    }

    const TArray<FSoftObjectPath> AllValidPaths = CollectAllValidPaths(SoftAssets);
    TArray<TSoftObjectPtr<TAsset>> CapturedAssets = SoftAssets;
    return LoadRawBatchAsync(
        AllValidPaths,
        Owner,
        [CapturedAssets = MoveTemp(CapturedAssets), TypedCallback = MoveTemp(OnAllLoaded)]() mutable {
            TypedCallback(ResolveTypedBatchAssets(CapturedAssets));
        },
        MoveTemp(OnProgress),
        CancellationToken,
        Priority
    );
}

template <typename TAsset>
TGameStatusResult<TAsset*> FGameAssetLoader::LoadSync(const TSoftObjectPtr<TAsset>& SoftAsset) {
    if (SoftAsset.IsNull()) {
        return TGameStatusResult<TAsset*>::Failure(
            Game::ModuleId::Asset,
            Game::AssetErrorCodes::InvalidAssetPath,
            TEXT("SoftAsset is null or invalid.")
        );
    }

    if (TAsset* Resident = SoftAsset.Get()) {
        return TGameStatusResult<TAsset*>::Success(Resident);
    }

    const FSoftObjectPath TargetPath = SoftAsset.ToSoftObjectPath();
    const TGameStatusResult<UObject*> RawResult = LoadRawSync(TargetPath);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TAsset*>::Failure(RawResult.GetError());
    }

    UObject* LoadedObject = RawResult.GetValue();
    if (TAsset* TypedAsset = CastCheckedAssetOrLog<TAsset>(TargetPath, LoadedObject)) {
        return TGameStatusResult<TAsset*>::Success(TypedAsset);
    }

    return TGameStatusResult<TAsset*>::Failure(
        Game::ModuleId::Asset,
        Game::AssetErrorCodes::TypeMismatch,
        FString::Printf(
            TEXT("Loaded asset '%s' (class '%s') is not compatible with expected type '%s'."),
            *TargetPath.ToString(),
            LoadedObject ? *LoadedObject->GetClass()->GetName() : TEXT("None"),
            *TAsset::StaticClass()->GetName()
        )
    );
}

template <typename TClass>
TGameStatusResult<TSubclassOf<TClass>> FGameAssetLoader::LoadClassSync(const TSoftClassPtr<TClass>& SoftClass) {
    if (SoftClass.IsNull()) {
        return TGameStatusResult<TSubclassOf<TClass>>::Failure(
            Game::ModuleId::Asset,
            Game::AssetErrorCodes::InvalidAssetPath,
            TEXT("SoftClass is null or invalid.")
        );
    }

    if (UClass* ResidentClass = SoftClass.Get()) {
        return TGameStatusResult<TSubclassOf<TClass>>::Success(TSubclassOf<TClass>(ResidentClass));
    }

    const FSoftObjectPath TargetPath = SoftClass.ToSoftObjectPath();
    const TGameStatusResult<UObject*> RawResult = LoadRawSync(TargetPath);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TSubclassOf<TClass>>::Failure(RawResult.GetError());
    }

    UObject* LoadedObject = RawResult.GetValue();
    if (TSubclassOf<TClass> CompatibleClass = CastCheckedClassOrLog<TClass>(TargetPath, LoadedObject)) {
        return TGameStatusResult<TSubclassOf<TClass>>::Success(CompatibleClass);
    }

    const UClass* ActualClass = Cast<UClass>(LoadedObject);
    return TGameStatusResult<TSubclassOf<TClass>>::Failure(
        Game::ModuleId::Asset,
        Game::AssetErrorCodes::TypeMismatch,
        FString::Printf(
            TEXT("Loaded object '%s' (type '%s') is not a subclass of '%s'."),
            *TargetPath.ToString(),
            ActualClass ? *ActualClass->GetName() : (LoadedObject ? *LoadedObject->GetClass()->GetName() : TEXT("None")),
            *TClass::StaticClass()->GetName()
        )
    );
}
