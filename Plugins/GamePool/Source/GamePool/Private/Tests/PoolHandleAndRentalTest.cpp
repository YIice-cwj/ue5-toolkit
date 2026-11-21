/**
 * @file PoolHandleAndRentalTest.cpp
 * @brief 池化对象安全代数句柄与 RAII 作用域租约（含移动赋值覆盖与带载荷租约）自动化测试
 */

#include "Subsystem/UObjectPoolSubsystem.h"

#include "Handle/TPooledObjectHandle.h"
#include "Handle/TScopedPoolRental.h"
#include "Tests/AGamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Tests/GamePoolTestUtils.h"

using Game::Pool::TestUtils::GetTestWorld;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolHandleGenerationSafetyTest,
    "Game.Pool.HandleGenerationSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证池化对象安全句柄的代数递增、解引用、归还失效与跨借出防野指针窜号覆写
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolHandleGenerationSafetyTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Pool->ClearPool(ActorClass);

    const TGameStatusResult<TPooledObjectHandle<AGamePoolableTestActor>> FirstHandleRes =
        Pool->AcquireActorHandle<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("首次借出句柄成功"), FirstHandleRes.IsSuccess());
    TPooledObjectHandle<AGamePoolableTestActor> FirstHandle = FirstHandleRes.GetValue();
    TestTrue(TEXT("句柄初始有效"), FirstHandle.IsValid());
    TestTrue(TEXT("句柄布尔转换有效"), static_cast<bool>(FirstHandle));
    AGamePoolableTestActor* FirstActor = FirstHandle.Get();
    TestNotNull(TEXT("句柄获取 Actor 指针有效"), FirstActor);
    TestEqual(TEXT("解引用 operator* 返回同一对象"), &(*FirstHandle), FirstActor);
    const uint32 FirstGen = FirstHandle.GetGeneration();
    TestTrue(TEXT("句柄代数大于零"), FirstGen > 0);

    TestTrue(TEXT("归还 Actor 成功"), Pool->ReleaseActor(FirstActor).IsSuccess());
    TestFalse(TEXT("归还后原句柄立即失效"), FirstHandle.IsValid());
    TestNull(TEXT("失效句柄获取对象返回 nullptr"), FirstHandle.Get());

    const TGameStatusResult<TPooledObjectHandle<AGamePoolableTestActor>> ReusedHandleRes =
        Pool->AcquireActorHandle<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("复用借出新句柄成功"), ReusedHandleRes.IsSuccess());
    TPooledObjectHandle<AGamePoolableTestActor> ReusedHandle = ReusedHandleRes.GetValue();
    TestTrue(TEXT("复用句柄有效"), ReusedHandle.IsValid());
    TestEqual(TEXT("复用同一个底层 Actor 实例"), ReusedHandle.Get(), FirstActor);
    TestTrue(TEXT("新句柄代数递增更新"), ReusedHandle.GetGeneration() > FirstGen);
    TestTrue(TEXT("C++20 合成 operator!= 判定不同世代句柄不等"), FirstHandle != ReusedHandle);

    TestFalse(TEXT("历史旧句柄面对已复用实例依然恒为无效"), FirstHandle.IsValid());
    TestNull(TEXT("历史旧句柄无法获取已被他人借用的实例"), FirstHandle.Get());

    TestTrue(TEXT("使用有效句柄归还成功"), Pool->ReleaseActor(ReusedHandle).IsSuccess());
    TestFalse(TEXT("归还后复用句柄同样失效"), ReusedHandle.IsValid());
    const TGameStatusResult<void> ExpiredReleaseRes = Pool->ReleaseActor(ReusedHandle);
    TestTrue(TEXT("使用已失效句柄执行归还返回失败"), ExpiredReleaseRes.IsFailure());
    TestEqual(TEXT("失效句柄错误归属 ModuleId::Pool"), ExpiredReleaseRes.GetError().ModuleId, Game::ModuleId::Pool);
    TestEqual(TEXT("失效句柄错误码为 InvalidHandle"), ExpiredReleaseRes.GetError().ErrorCode, Game::PoolErrorCodes::InvalidHandle);

    const TSubclassOf<UObject> ObjClass = UGamePoolableTestObject::StaticClass();
    Pool->ClearObjectPool(ObjClass);

    const TGameStatusResult<TPooledObjectHandle<UGamePoolableTestObject>> ObjHandleRes =
        Pool->AcquireObjectHandle<UGamePoolableTestObject>(ObjClass);
    TestTrue(TEXT("借出 UObject 句柄成功"), ObjHandleRes.IsSuccess());
    TPooledObjectHandle<UGamePoolableTestObject> ObjHandle = ObjHandleRes.GetValue();
    TestTrue(TEXT("UObject 句柄有效"), ObjHandle.IsValid());
    UGamePoolableTestObject* Obj = ObjHandle.Get();
    TestNotNull(TEXT("UObject 句柄实例非空"), Obj);

    TestTrue(TEXT("通过句柄归还 UObject 成功"), Pool->ReleaseObject(ObjHandle).IsSuccess());
    TestFalse(TEXT("归还后 UObject 句柄失效"), ObjHandle.IsValid());
    TestNull(TEXT("失效 UObject 句柄返回 nullptr"), ObjHandle.Get());

    TSet<TPooledObjectHandle<AGamePoolableTestActor>> HandleSet;
    HandleSet.Add(FirstHandle);
    TestTrue(TEXT("句柄支持无缝存入 TSet 并检索"), HandleSet.Contains(FirstHandle));

    TMap<TPooledObjectHandle<AGamePoolableTestActor>, FString> HandleMap;
    HandleMap.Add(FirstHandle, TEXT("ActorHandle"));
    TestEqual(TEXT("句柄支持作为 TMap 键值映射"), HandleMap.FindRef(FirstHandle), FString(TEXT("ActorHandle")));

    Pool->ClearPool(ActorClass);
    Pool->ClearObjectPool(ObjClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolScopedRentalTest,
    "Game.Pool.ScopedRental",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证池化对象 RAII 作用域租约卫士的自动归还、移动构造/移动赋值、带载荷租约、提前归还与脱钩
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolScopedRentalTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Pool->ClearPool(ActorClass);

    {
        const int32 ScopedPayload = 2026;
        TGameStatusResult<TScopedPoolRental<AGamePoolableTestActor>> RentalRes =
            Pool->AcquireScopedActorWithPayload<AGamePoolableTestActor>(ActorClass, FTransform::Identity, ScopedPayload);
        TestTrue(TEXT("借出带载荷租约成功"), RentalRes.IsSuccess());
        TScopedPoolRental<AGamePoolableTestActor> Rental = RentalRes.ExtractValue();
        TestTrue(TEXT("作用域内租约有效"), Rental.IsValid());
        TestNotNull(TEXT("作用域内租约指针非空"), Rental.Get());
        TestEqual(TEXT("带载荷租约正确注入载荷值"), (*Rental).LastReceivedPayloadValue, 2026);

        const FPoolStats RentalActiveStats = Pool->GetPoolStats(ActorClass);
        TestEqual(TEXT("租约存续期间活跃数为 1"), RentalActiveStats.ActiveCount, 1);
        TestEqual(TEXT("租约存续期间空闲数为 0"), RentalActiveStats.IdleCount, 0);
    }

    const FPoolStats RentalReleasedStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("离开租约作用域后自动归还活跃数为 0"), RentalReleasedStats.ActiveCount, 0);
    TestEqual(TEXT("离开租约作用域后自动入池空闲数为 1"), RentalReleasedStats.IdleCount, 1);

    TScopedPoolRental<AGamePoolableTestActor> RentalA = Pool->AcquireScopedActor<AGamePoolableTestActor>(ActorClass).ExtractValue();
    TScopedPoolRental<AGamePoolableTestActor> RentalB = Pool->AcquireScopedActor<AGamePoolableTestActor>(ActorClass).ExtractValue();
    TestEqual(TEXT("同时持有双租约活跃数为 2"), Pool->GetPoolStats(ActorClass).ActiveCount, 2);

    AGamePoolableTestActor* PtrB = RentalB.Get();
    RentalA = MoveTemp(RentalB);
    TestFalse(TEXT("移动赋值后源租约失活"), RentalB.IsValid());
    TestEqual(TEXT("移动赋值后目标租约指向原 B"), RentalA.Get(), PtrB);
    TestEqual(TEXT("移动赋值自动归还被覆盖的旧租约 A，活跃数降为 1"), Pool->GetPoolStats(ActorClass).ActiveCount, 1);

    TScopedPoolRental<AGamePoolableTestActor> TargetRental = MoveTemp(RentalA);
    TestFalse(TEXT("移动构造后源租约失活"), RentalA.IsValid());
    TestTrue(TEXT("移动构造后目标租约有效"), TargetRental.IsValid());

    TargetRental.EarlyRelease();
    TestFalse(TEXT("提前显式归还后租约失效"), TargetRental.IsValid());
    const FPoolStats EarlyReleaseStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("提前归还后活跃数归零"), EarlyReleaseStats.ActiveCount, 0);
    TestEqual(TEXT("提前归还后空闲数为 2"), EarlyReleaseStats.IdleCount, 2);

    TScopedPoolRental<AGamePoolableTestActor> DetachRental =
        Pool->AcquireScopedActor<AGamePoolableTestActor>(ActorClass).ExtractValue();
    AGamePoolableTestActor* DetachedActor = DetachRental.ReleaseAndDetach();
    TestFalse(TEXT("脱钩后租约失效"), DetachRental.IsValid());
    TestNotNull(TEXT("脱钩返回的 Actor 指针有效"), DetachedActor);
    TestTrue(TEXT("手动归还脱钩 Actor 成功"), Pool->ReleaseActor(DetachedActor).IsSuccess());

    const TSubclassOf<UObject> ObjClass = UGamePoolableTestObject::StaticClass();
    Pool->ClearObjectPool(ObjClass);
    {
        TGameStatusResult<TScopedPoolRental<UGamePoolableTestObject>> ObjRentalRes =
            Pool->AcquireScopedObject<UGamePoolableTestObject>(ObjClass);
        TestTrue(TEXT("借出 UObject 租约成功"), ObjRentalRes.IsSuccess());
        TScopedPoolRental<UGamePoolableTestObject> ObjRental = ObjRentalRes.ExtractValue();
        TestTrue(TEXT("UObject 租约有效"), ObjRental.IsValid());
    }
    const FPoolStats ObjStats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("UObject 离开作用域后自动归还活跃数为 0"), ObjStats.ActiveCount, 0);
    TestEqual(TEXT("UObject 离开作用域后自动归还空闲数为 1"), ObjStats.IdleCount, 1);

    Pool->ClearPool(ActorClass);
    Pool->ClearObjectPool(ObjClass);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
