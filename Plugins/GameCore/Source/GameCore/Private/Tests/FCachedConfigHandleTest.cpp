/**
 * @file FCachedConfigHandleTest.cpp
 * @brief 零哈希高性能配置句柄 TCachedConfigHandle 自动化测试
 */

#include "Config/TCachedConfigHandle.h"
#include "Config/FConfigRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"

#include <atomic>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCachedConfigHandleBasicTest, "Game.Core.Config.CachedHandle.Basic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 基础功能测试：未注册回退、延迟注册感知、运行时更新、类型转换与解引用
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FCachedConfigHandleBasicTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry::ClearWorldRegistriesForTesting();
    FConfigRegistry& Registry = FConfigRegistry::Get();

    static const FName FloatKey(TEXT("UnitTest.CachedHandle.MoveSpeed"));
    static const FName VecKey(TEXT("UnitTest.CachedHandle.SpawnOffset"));

    TCachedConfigHandle<float> SpeedHandle(FloatKey, 300.0f);
    TestTrue(TEXT("句柄必须为有效有效键名状态"), SpeedHandle.IsValid());
    TestFalse(TEXT("未注册前句柄必须处于未绑定真实值状态"), SpeedHandle.IsBound());
    TestEqual(TEXT("未注册时必须返回指定的 Fallback 值"), SpeedHandle.Get(), 300.0f);
    TestEqual(TEXT("GetKey 必须返回正确的键名"), SpeedHandle.GetKey(), FloatKey);
    TestEqual(TEXT("GetFallback 必须返回指定的初始 Fallback"), SpeedHandle.GetFallback(), 300.0f);

    Registry.RegisterConfig<float>(FloatKey, 600.0f);
    TestTrue(TEXT("注册后句柄应感知到已绑定有效值"), SpeedHandle.IsBound());
    TestEqual(TEXT("注册后句柄应自同步为已注册值"), SpeedHandle.Get(), 600.0f);

    const ESetConfigResult SetResult = Registry.SetConfig<float>(FloatKey, 750.0f);
    TestEqual(TEXT("SetConfig 必须成功写入"), SetResult, ESetConfigResult::Applied);
    TestEqual(TEXT("SetConfig 后句柄必须更新为最新值"), SpeedHandle.Get(), 750.0f);

    const float ImplicitVal = SpeedHandle;
    TestEqual(TEXT("隐式类型转换必须返回当前有效值"), ImplicitVal, 750.0f);
    TestEqual(TEXT("解引用运算符必须返回当前有效值"), *SpeedHandle, 750.0f);
    TestTrue(TEXT("相等运算符必须返回 true"), SpeedHandle == 750.0f);
    TestFalse(TEXT("不等运算符必须返回 false"), SpeedHandle != 750.0f);

    TCachedConfigHandle<FVector> VectorHandle(VecKey, FVector(10.0, 20.0, 30.0));
    TestEqual(TEXT("复合向量句柄初始应为 Fallback"), VectorHandle.Get(), FVector(10.0, 20.0, 30.0));
    Registry.RegisterConfig<FVector>(VecKey, FVector(100.0, 200.0, 300.0));
    TestEqual(TEXT("复合向量句柄注册后应更新"), VectorHandle.Get(), FVector(100.0, 200.0, 300.0));
    TestEqual(TEXT("箭头运算符直连访问成员 X"), VectorHandle->X, 100.0);
    TestEqual(TEXT("箭头运算符直连访问成员 Y"), VectorHandle->Y, 200.0);
    TestEqual(TEXT("箭头运算符直连访问成员 Z"), VectorHandle->Z, 300.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCachedConfigHandleObserverAndCopyTest, "Game.Core.Config.CachedHandle.ObserverAndCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 进阶功能测试：自定义回调触发、句柄浅拷贝共享与生命周期隔离
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FCachedConfigHandleObserverAndCopyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry::ClearWorldRegistriesForTesting();
    FConfigRegistry& Registry = FConfigRegistry::Get();

    static const FName IntKey(TEXT("UnitTest.CachedHandle.PlayerLevel"));
    Registry.RegisterConfig<int32>(IntKey, 1);

    TCachedConfigHandle<int32> LevelHandle(IntKey, 0);
    TestTrue(TEXT("已注册键句柄构造时即处于绑定状态"), LevelHandle.IsBound());
    TestEqual(TEXT("已注册键句柄值必须为注册值"), LevelHandle.Get(), 1);

    int32 CallbackInvocations = 0;
    int32 LastObservedValue = 0;
    LevelHandle.SetOnChanged([&](int32 NewVal) {
        ++CallbackInvocations;
        LastObservedValue = NewVal;
    });

    Registry.SetConfig<int32>(IntKey, 5);
    TestEqual(TEXT("SetOnChanged 闭包必须被触发 1 次"), CallbackInvocations, 1);
    TestEqual(TEXT("通知值必须与新值一致"), LastObservedValue, 5);
    TestEqual(TEXT("句柄自身值必须更新"), LevelHandle.Get(), 5);

    TCachedConfigHandle<int32> LevelHandleCopy = LevelHandle;
    TestTrue(TEXT("拷贝副本同样有效"), LevelHandleCopy.IsValid());
    TestEqual(TEXT("拷贝副本值必须与源句柄一致"), LevelHandleCopy.Get(), 5);

    Registry.SetConfig<int32>(IntKey, 10);
    TestEqual(TEXT("源句柄必须更新为 10"), LevelHandle.Get(), 10);
    TestEqual(TEXT("共享内部状态的拷贝副本必须同步为 10"), LevelHandleCopy.Get(), 10);
    TestEqual(TEXT("共享回调闭包被触发第 2 次"), CallbackInvocations, 2);

    LevelHandle.Reset();
    TestFalse(TEXT("Reset 后句柄必须变为无效"), LevelHandle.IsValid());
    TestEqual(TEXT("Reset 后 Get 必须返回默认构造值"), LevelHandle.Get(), 0);

    static const FName ResetKey(TEXT("UnitTest.CachedHandle.ResetKey"));
    Registry.RegisterConfig<int32>(ResetKey, 88);
    LevelHandle.Reset(ResetKey, 0);
    TestTrue(TEXT("Reset 全参重置后句柄有效且绑定到新键"), LevelHandle.IsValid() && LevelHandle.IsBound());
    TestEqual(TEXT("Reset 全参重置后取新键值"), LevelHandle.Get(), 88);

    LevelHandle.Rebind(nullptr);
    TestTrue(TEXT("Rebind 重新绑定后依然有效"), LevelHandle.IsValid() && LevelHandle.IsBound());
    TestEqual(TEXT("Rebind 后取值正确"), LevelHandle.Get(), 88);

    TCachedConfigHandle<int32> LateBoundHandle;
    int32 LateCallbackHits = 0;
    int32 LateCallbackValue = 0;
    LateBoundHandle.SetOnChanged([&](int32 NewVal) {
        ++LateCallbackHits;
        LateCallbackValue = NewVal;
    });
    static const FName LateKeyA(TEXT("UnitTest.CachedHandle.LateKeyA"));
    static const FName LateKeyB(TEXT("UnitTest.CachedHandle.LateKeyB"));
    Registry.RegisterConfig<int32>(LateKeyA, 11);
    Registry.RegisterConfig<int32>(LateKeyB, 22);
    LateBoundHandle.Reset(LateKeyA, 0);
    Registry.SetConfig<int32>(LateKeyA, 33);
    TestEqual(TEXT("默认构造后先 SetOnChanged 再 Reset(Key) 回调生效"), LateCallbackHits, 1);
    TestEqual(TEXT("回调收到 LateKeyA 更新值 33"), LateCallbackValue, 33);

    LateBoundHandle.Reset(LateKeyB, 0);
    Registry.SetConfig<int32>(LateKeyB, 44);
    TestEqual(TEXT("已绑定句柄再次 Reset(NewKey) 后既有回调依然保留生效"), LateCallbackHits, 2);
    TestEqual(TEXT("回调收到 LateKeyB 更新值 44"), LateCallbackValue, 44);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCachedConfigHandleConcurrencyAndAtomicTest,
    "Game.Core.Config.CachedHandle.ConcurrencyAndAtomicFastPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证标量原子快路径与复合类型在并发读取、热更及 Reset(NewKey) 下的线程安全与旧键隔离
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FCachedConfigHandleConcurrencyAndAtomicTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FConfigRegistry::ClearWorldRegistriesForTesting();
    FConfigRegistry& Registry = FConfigRegistry::Get();

    static const FName KeyA(TEXT("UnitTest.CachedHandle.ConcurrentA"));
    static const FName KeyB(TEXT("UnitTest.CachedHandle.ConcurrentB"));
    static const FName StrKey(TEXT("UnitTest.CachedHandle.ConcurrentStr"));

    Registry.SetConfig<float>(KeyA, 10.0f);
    Registry.SetConfig<float>(KeyB, 20.0f);
    Registry.SetConfig<FString>(StrKey, TEXT("Alpha"));

    TCachedConfigHandle<float> ScalarHandle(KeyA, 0.0f);
    TCachedConfigHandle<FString> StringHandle(StrKey, TEXT("Fallback"));

    std::atomic<int32> ValidReads{0};
    ParallelFor(8, [&](int32 WorkerIndex) {
        if (WorkerIndex == 0) {
            for (int32 Iter = 0; Iter < 200; ++Iter) {
                Registry.SetConfig<float>(KeyA, static_cast<float>(Iter));
                Registry.SetConfig<float>(KeyB, static_cast<float>(1000 + Iter));
                ScalarHandle.Reset((Iter & 1) == 0 ? KeyA : KeyB, -1.0f);
                Registry.SetConfig<FString>(StrKey, (Iter & 1) == 0 ? TEXT("Alpha") : TEXT("Beta"));
            }
        } else {
            for (int32 Iter = 0; Iter < 500; ++Iter) {
                const float Val = ScalarHandle.Get();
                const FName Key = ScalarHandle.GetKey();
                const bool bValid = ScalarHandle.IsValid();
                const FString StrVal = StringHandle.Get();
                if (bValid && (Key == KeyA || Key == KeyB) && Val >= -1.0f && !StrVal.IsEmpty()) {
                    ValidReads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    TestEqual(TEXT("全部并发读者在 Reset 与热更交替期间均读到合法状态"), ValidReads.load(std::memory_order_acquire), 7 * 500);

    ScalarHandle.Reset(KeyB, 0.0f);
    Registry.SetConfig<float>(KeyB, 777.0f);
    Registry.SetConfig<float>(KeyA, 123.0f);
    TestEqual(TEXT("切换至 KeyB 后旧键 KeyA 的更新不会污染句柄值"), ScalarHandle.Get(), 777.0f);
    TestEqual(TEXT("GetKey 准确返回当前绑定键 KeyB"), ScalarHandle.GetKey(), KeyB);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
