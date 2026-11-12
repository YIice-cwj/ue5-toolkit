// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/ObjectPoolSubsystem.h"

#include "Tests/GamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

namespace {

/**
 * @brief 获取测试世界指针
 * @return 当前有效的 UWorld 指针，若不存在则返回 nullptr
 */
UWorld* GetTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World()) {
            return Context.World();
        }
    }
    return nullptr;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNonActorUObjectPoolTest,
    "Game.Pool.UObjectPoolLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证非 Actor 纯 UObject 对象池预热、借出、复用、容量上限裁剪、回调重入防护及 AActor 类型隔离
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameNonActorUObjectPoolTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    const TSubclassOf<UObject> ObjClass = UGamePoolableTestObject::StaticClass();
    Pool->ClearObjectPool(ObjClass);
    Pool->SetMaxObjectPoolCapacity(ObjClass, 100);

    TestTrue(
        TEXT("拒绝使用 AcquireObject 借出 AActor 派生类"),
        Pool->AcquireObject(AGamePoolableTestActor::StaticClass()).IsFailure()
    );
    TestTrue(TEXT("空类型借出返回失败"), Pool->AcquireObject(nullptr).IsFailure());

    Pool->PrewarmObjectPool(ObjClass, 3);
    FPoolStats Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("UObject 预热后空闲数为 3"), Stats.IdleCount, 3);
    TestEqual(TEXT("UObject 预热后活跃数为 0"), Stats.ActiveCount, 0);
    TestEqual(TEXT("UObject 预热后总数为 3"), Stats.TotalCount, 3);

    Pool->SetMaxObjectPoolCapacity(ObjClass, 1);
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("收缩 UObject 池容量上限立即裁剪空闲实例至 1"), Stats.IdleCount, 1);

    const TGameStatusResult<UGamePoolableTestObject*> ObjARes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("首次借出纯 UObject 成功"), ObjARes.IsSuccess());
    UGamePoolableTestObject* ObjA = ObjARes.GetValue();
    TestNotNull(TEXT("首次借出纯 UObject 实例有效"), ObjA);
    if (!ObjA) {
        return false;
    }
    TestEqual(TEXT("借出触发纯 UObject 的 OnAcquireFromPool 回调"), ObjA->AcquireCount, 1);
    ObjA->PayloadValue = 42;

    const TGameStatusResult<UGamePoolableTestObject*> ObjBRes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("借出第二个实例成功"), ObjBRes.IsSuccess());
    UGamePoolableTestObject* ObjB = ObjBRes.GetValue();
    TestNotNull(TEXT("空闲耗尽时自动 NewObject 扩容借出第二个实例"), ObjB);
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("两个活跃实例计数为 2"), Stats.ActiveCount, 2);
    TestEqual(TEXT("空闲数为 0"), Stats.IdleCount, 0);

    TestTrue(TEXT("归还 ObjA 成功"), Pool->ReleaseObject(ObjA).IsSuccess());
    TestEqual(TEXT("归还触发纯 UObject 的 OnReleaseToPool 回调"), ObjA->ReleaseCount, 1);
    TestTrue(TEXT("归还 ObjB 成功"), Pool->ReleaseObject(ObjB).IsSuccess());

    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("受容量上限 1 约束仅保留 1 个空闲实例"), Stats.IdleCount, 1);
    TestEqual(TEXT("全部归还后活跃数为 0"), Stats.ActiveCount, 0);

    const TGameStatusResult<UGamePoolableTestObject*> ReusedObjRes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("复用借出成功"), ReusedObjRes.IsSuccess());
    UGamePoolableTestObject* ReusedObj = ReusedObjRes.GetValue();
    TestEqual(TEXT("再次借出精确复用池内缓存的同一 UObject 指针"), ReusedObj, ObjA);
    TestEqual(TEXT("复用实例累计借出计数为 2"), ReusedObj->AcquireCount, 2);
    TestTrue(TEXT("归还复用对象成功"), Pool->ReleaseObject(ReusedObj).IsSuccess());

    UGamePoolableTestObject* ExternalObj = NewObject<UGamePoolableTestObject>(World);
    TestTrue(TEXT("非池属外部 UObject 归还返回失败"), Pool->ReleaseObject(ExternalObj).IsFailure());
    TestEqual(TEXT("非池属外部 UObject 归还不触发回调"), ExternalObj->ReleaseCount, 0);

    Pool->SetMaxObjectPoolCapacity(ObjClass, 10);
    ReusedObj->AcquireCallback = [Pool](UGamePoolableTestObject* Self) {
        Pool->ReleaseObject(Self);
    };
    const TGameStatusResult<UGamePoolableTestObject*> SelfReleasedRes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("OnAcquire 内自归还的 UObject 返回失败"), SelfReleasedRes.IsFailure());
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("自归还后活跃数为 0"), Stats.ActiveCount, 0);
    TestEqual(TEXT("自归还后空闲数为 1"), Stats.IdleCount, 1);

    ReusedObj->AcquireCallback = [](UGamePoolableTestObject* Self) {
        Self->MarkAsGarbage();
    };
    const TGameStatusResult<UGamePoolableTestObject*> SelfDestroyedRes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("OnAcquire 内被标记回收的 UObject 返回失败"), SelfDestroyedRes.IsFailure());
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("自回收后活跃数归零"), Stats.ActiveCount, 0);
    TestEqual(TEXT("自回收后空闲数归零"), Stats.IdleCount, 0);

    Pool->PrewarmObjectPool(ObjClass, 2);
    Pool->ClearAllPools();
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("ClearAllPools 同步清空所有 UObject 空闲池"), Stats.IdleCount, 0);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
