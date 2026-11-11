/**
 * @file FGameServiceLocatorTest.cpp
 * @brief 纯 C++ 线程安全服务定位器单元测试
 */

#include "Service/FGameServiceLocator.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"

namespace {

struct ITestAudioService {
    virtual ~ITestAudioService() = default;
    virtual int32 GetVolume() const = 0;
};

struct FConcreteAudioService : public ITestAudioService {
    explicit FConcreteAudioService(int32 InVolume) : Volume(InVolume) {}
    int32 GetVolume() const override { return Volume; }
    int32 Volume = 100;
};

struct ITestNetworkService {
    virtual ~ITestNetworkService() = default;
    virtual FString GetRegion() const = 0;
};

struct FConcreteNetworkService : public ITestNetworkService {
    explicit FConcreteNetworkService(FString InRegion) : Region(MoveTemp(InRegion)) {}
    FString GetRegion() const override { return Region; }
    FString Region;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameServiceLocatorTypeAndNamedTest,
    "Game.Core.Service.ServiceLocator.TypeAndNamed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证类型服务与具名服务的注册、解析、注销与计数
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameServiceLocatorTypeAndNamedTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameServiceLocator& Locator = FGameServiceLocator::Get();
    Locator.Reset();

    TestEqual(TEXT("初始状态服务总数为 0"), Locator.Num(), 0);
    TestFalse(TEXT("未注册类型服务解析返回空"), Locator.HasService<ITestAudioService>());
    TestNull(TEXT("未注册类型服务 Resolve 为空指针"), Locator.Resolve<ITestAudioService>().Get());

    TSharedRef<ITestAudioService, ESPMode::ThreadSafe> AudioImpl =
        MakeShared<FConcreteAudioService, ESPMode::ThreadSafe>(80);
    Locator.RegisterService<ITestAudioService>(AudioImpl);

    TestTrue(TEXT("注册后 HasService 为真"), Locator.HasService<ITestAudioService>());
    TestEqual(TEXT("服务计数为 1"), Locator.Num(), 1);

    TSharedPtr<ITestAudioService, ESPMode::ThreadSafe> ResolvedAudio = Locator.Resolve<ITestAudioService>();
    TestNotNull(TEXT("成功解析出音频服务"), ResolvedAudio.Get());
    TestEqual(TEXT("音频服务接口调用正常"), ResolvedAudio->GetVolume(), 80);

    const FName RegionNorth(TEXT("Region.North"));
    const FName RegionSouth(TEXT("Region.South"));

    TSharedRef<ITestNetworkService, ESPMode::ThreadSafe> NetNorth =
        MakeShared<FConcreteNetworkService, ESPMode::ThreadSafe>(TEXT("NorthAmerica"));
    TSharedRef<ITestNetworkService, ESPMode::ThreadSafe> NetSouth =
        MakeShared<FConcreteNetworkService, ESPMode::ThreadSafe>(TEXT("SouthAmerica"));

    Locator.RegisterService<ITestNetworkService>(NetNorth, RegionNorth);
    Locator.RegisterService<ITestNetworkService>(NetSouth, RegionSouth);

    TestTrue(TEXT("包含 North 服务"), Locator.HasService(RegionNorth));
    TestTrue(TEXT("包含 South 服务"), Locator.HasService(RegionSouth));
    TestEqual(TEXT("服务计数为 3"), Locator.Num(), 3);

    TSharedPtr<ITestNetworkService, ESPMode::ThreadSafe> ResolvedNorth =
        Locator.Resolve<ITestNetworkService>(RegionNorth);
    TSharedPtr<ITestNetworkService, ESPMode::ThreadSafe> ResolvedSouth =
        Locator.Resolve<ITestNetworkService>(RegionSouth);

    TestNotNull(TEXT("成功解析 North"), ResolvedNorth.Get());
    TestNotNull(TEXT("成功解析 South"), ResolvedSouth.Get());
    TestEqual(TEXT("North 区域匹配"), ResolvedNorth->GetRegion(), TEXT("NorthAmerica"));
    TestEqual(TEXT("South 区域匹配"), ResolvedSouth->GetRegion(), TEXT("SouthAmerica"));

    TestTrue(TEXT("注销类型服务成功"), Locator.UnregisterService<ITestAudioService>());
    TestFalse(TEXT("再次查询类型服务已不存在"), Locator.HasService<ITestAudioService>());

    TestTrue(TEXT("注销具名服务成功"), Locator.UnregisterService(RegionNorth));
    TestFalse(TEXT("再次查询具名服务已不存在"), Locator.HasService(RegionNorth));

    Locator.Reset();
    TestEqual(TEXT("Reset 后服务计数归零"), Locator.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameServiceLocatorConcurrencyTest,
    "Game.Core.Service.ServiceLocator.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发高频只读解析的线程安全性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameServiceLocatorConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameServiceLocator& Locator = FGameServiceLocator::Get();
    Locator.Reset();

    TSharedRef<ITestAudioService, ESPMode::ThreadSafe> AudioImpl =
        MakeShared<FConcreteAudioService, ESPMode::ThreadSafe>(99);
    Locator.RegisterService<ITestAudioService>(AudioImpl);

    constexpr int32 IterationCount = 5000;
    std::atomic<int32> SuccessCount{0};

    ParallelFor(IterationCount, [&Locator, &SuccessCount]([[maybe_unused]] int32 Index) {
        TSharedPtr<ITestAudioService, ESPMode::ThreadSafe> Service = Locator.Resolve<ITestAudioService>();
        if (Service.IsValid() && Service->GetVolume() == 99) {
            SuccessCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    TestEqual(TEXT("多线程并发解析全部成功且数据无撕裂"), SuccessCount.load(), IterationCount);

    Locator.Reset();
    return true;
}

#endif
