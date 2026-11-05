/**
 * @file GameFeedbackNetworkTest.cpp
 * @brief 反馈系统网络序列化、定义契约与 Dedicated Server 服务自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Component/GameFeedbackNetworkDispatcherComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/PackageMapClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Network/GameFeedbackEventNet.h"
#include "Network/GameFeedbackReceiverComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "System/GameFeedbackHitStopService.h"
#include "Time/TimeDilationSubsystem.h"
#include "UObject/Package.h"

namespace {

/**
 * @brief 创建具有稳定资产路径的测试 Definition
 * @param ObjectPath 完整对象路径（Package.Object）
 * @return 测试 Definition
 */
UGameFeedbackDefinition* CreateNetworkDefinitionAtPath(const FString& ObjectPath) {
    FString PackageName;
    FString ObjectName;
    ObjectPath.Split(TEXT("."), &PackageName, &ObjectName);
    return NewObject<UGameFeedbackDefinition>(CreatePackage(*PackageName), *ObjectName);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackNetSerializeTest,
    "Game.Feedback.NetSerializeByteBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证真实 PackageMap 下的完整网络载荷、对象与骨骼还原及字节预算
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackNetSerializeTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    UDemoNetDriver* Driver = NewObject<UDemoNetDriver>(GetTransientPackage());
    Driver->World = World;
    USimulatedClientNetConnection* Connection = NewObject<USimulatedClientNetConnection>(Driver);
    Connection->Driver = Driver;
    Connection->SetInternalAck(true);
    TSharedRef<FNetGUIDCache> GuidCache = MakeShared<FNetGUIDCache>(Driver);
    UPackageMapClient* PackageMap = NewObject<UPackageMapClient>(Connection);
    PackageMap->Initialize(Connection, GuidCache);
    Connection->PackageMap = PackageMap;

    AActor* Instigator = World->SpawnActor<AActor>();
    AActor* DamageCauser = World->SpawnActor<AActor>();
    AActor* Target = World->SpawnActor<AActor>();
    USkeletalMeshComponent* HitComponent = NewObject<USkeletalMeshComponent>(Target);
    Target->SetRootComponent(HitComponent);
    USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
    TestNotNull(TEXT("骨骼测试资产可用"), SkeletalMesh);
    if (!SkeletalMesh) {
        World->DestroyWorld(false);
        World->RemoveFromRoot();
        return false;
    }
    HitComponent->SetSkeletalMeshAsset(SkeletalMesh);
    HitComponent->SetIsReplicated(true);
    HitComponent->RegisterComponent();
    TestTrue(TEXT("骨骼测试资产至少包含两根骨骼"), HitComponent->GetNumBones() > 1);
    if (HitComponent->GetNumBones() <= 1) {
        World->DestroyWorld(false);
        World->RemoveFromRoot();
        return false;
    }

    FGameFeedbackSignal Signal;
    Signal.Instigator = Instigator;
    Signal.DamageCauser = DamageCauser;
    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(100.0f, 200.0f, 300.0f);
    Signal.HitResult.ImpactNormal = FVector::UpVector;
    Signal.HitResult.Component = HitComponent;
    Signal.HitResult.BoneName = HitComponent->GetBoneName(1);
    Signal.ImpactDirection = FVector::RightVector;
    Signal.TraceDirection = FVector::ForwardVector;
    Signal.PrimaryChannel = UGameplayTagsManager::Get().RequestGameplayTag(FName(TEXT("Item.Consumable.Potion")));
    Signal.Magnitude = 125.5f;
    Signal.bLethal = true;
    Signal.PredictionEventId = 417;
    for (int32 Index = 0; Index < 8; ++Index) {
        Signal.FeedbackTags.AddTag(UGameplayTagsManager::Get().AddNativeGameplayTag(
            FName(*FString::Printf(TEXT("Feedback.Test.Net.Tag%d"), Index))
        ));
    }

    FGameFeedbackEventNet NetEvent = FGameFeedbackEventNet::FromSignal(Signal, 0x10203040u, 0x50607080u);

    FGameFeedbackEventNet MissingMapEvent = NetEvent;
    FNetBitWriter MissingMapWriter(nullptr, 1024);
    bool bMissingMapSuccess = true;
    TestFalse(TEXT("缺少 PackageMap 必须明确失败"), MissingMapEvent.NetSerialize(MissingMapWriter, nullptr, bMissingMapSuccess));
    TestFalse(TEXT("缺少 PackageMap 不得置成功标记"), bMissingMapSuccess);

    FNetBitWriter BitWriter(PackageMap, 1024);
    bool bSuccess = false;
    TestTrue(TEXT("完整事件序列化应当成功"), NetEvent.NetSerialize(BitWriter, PackageMap, bSuccess));
    TestTrue(TEXT("完整事件序列化成功标记置位"), bSuccess);
    const int32 FullBytes = BitWriter.GetNumBytes();
    AddInfo(FString::Printf(TEXT("真实 PackageMap 完整事件载荷：%d 字节"), FullBytes));
    TestTrue(TEXT("完整网络载荷必须满足预算 <= 96 字节"), FullBytes <= 96);

    FGameFeedbackEventNet ReadEvent;
    FNetBitReader BitReader(PackageMap, BitWriter.GetData(), BitWriter.GetNumBits());
    bool bReadSuccess = false;
    TestTrue(TEXT("完整事件反序列化应当成功"), ReadEvent.NetSerialize(BitReader, PackageMap, bReadSuccess));
    TestTrue(TEXT("完整事件反序列化成功标记置位"), bReadSuccess);
    TestEqual(TEXT("DefinitionID 应准确还原"), ReadEvent.DefinitionId, NetEvent.DefinitionId);
    TestEqual(TEXT("映射版本应准确还原"), ReadEvent.DefinitionMappingVersion, NetEvent.DefinitionMappingVersion);
    TestEqual(TEXT("主通道索引应准确还原"), ReadEvent.PrimaryChannelIndex, NetEvent.PrimaryChannelIndex);
    const bool bFastReplication = UGameplayTagsManager::Get().ShouldUseFastReplication();
    if (bFastReplication) {
        TestNotEqual(TEXT("启用快速复制时必须携带有效主通道索引"), NetEvent.PrimaryChannelIndex, MAX_uint16);
    } else {
        TestEqual(TEXT("未启用快速复制时主通道索引使用哨兵"), NetEvent.PrimaryChannelIndex, MAX_uint16);
    }
    TestEqual(TEXT("状态掩码应准确还原"), ReadEvent.StateFlags, NetEvent.StateFlags);
    TestEqual(TEXT("预测事件 ID 应准确还原"), ReadEvent.PredictionEventId, Signal.PredictionEventId);
    TestEqual(TEXT("骨骼索引应准确还原"), ReadEvent.BoneIndex, NetEvent.BoneIndex);
    TestNotEqual(TEXT("有效骨骼不得使用无骨骼哨兵"), NetEvent.BoneIndex, MAX_uint16);
    TestTrue(TEXT("半精度浮点伤害还原误差应当小于 0.1"), FMath::Abs(ReadEvent.GetDamage() - 125.5f) < 0.1f);

    NetEvent.SetDamage(100000.0f);
    TestTrue(TEXT("极大伤害应安全饱和截断在 65504 范围内"), NetEvent.GetDamage() <= 65504.0f && NetEvent.GetDamage() >= 65400.0f);

    const FGameFeedbackSignal Restored = FGameFeedbackEventNet::ToSignal(ReadEvent);
    if (bFastReplication) {
        TestEqual(TEXT("主通道应准确还原"), Restored.PrimaryChannel, Signal.PrimaryChannel);
    } else {
        TestFalse(TEXT("未启用快速复制时客户端不重建主通道"), Restored.PrimaryChannel.IsValid());
    }
    TestEqual(TEXT("Instigator 应准确还原"), Restored.Instigator.Get(), Instigator);
    TestEqual(TEXT("DamageCauser 应准确还原"), Restored.DamageCauser.Get(), DamageCauser);
    TestEqual(TEXT("Target 应准确还原"), Restored.Target.Get(), Target);
    TestTrue(TEXT("HitComponent 应准确还原"), Restored.HitResult.Component.Get() == HitComponent);
    TestEqual(TEXT("BoneName 应准确还原"), Restored.HitResult.BoneName, Signal.HitResult.BoneName);
    TestEqual(TEXT("TraceDirection 应准确还原"), Restored.TraceDirection, Signal.TraceDirection);
    TestEqual(TEXT("PredictionEventId 应准确还原到信号"), Restored.PredictionEventId, Signal.PredictionEventId);
    TestEqual(TEXT("远端不再重建 FeedbackTags"), Restored.FeedbackTags.Num(), 0);

    FGameFeedbackSignal TypicalSignal;
    TypicalSignal.Instigator = Instigator;
    TypicalSignal.Target = Target;
    TypicalSignal.HitResult.ImpactPoint = FVector(1.0f, 2.0f, 3.0f);
    TypicalSignal.HitResult.ImpactNormal = FVector::UpVector;
    TypicalSignal.ImpactDirection = FVector::ForwardVector;
    TypicalSignal.TraceDirection = FVector::ForwardVector;
    TypicalSignal.PrimaryChannel = Signal.PrimaryChannel;
    TypicalSignal.Magnitude = 10.0f;
    FGameFeedbackEventNet TypicalEvent = FGameFeedbackEventNet::FromSignal(TypicalSignal, 1u, 2u);
    FNetBitWriter TypicalWriter(PackageMap, 1024);
    bool bTypicalSuccess = false;
    TestTrue(TEXT("典型事件序列化应当成功"), TypicalEvent.NetSerialize(TypicalWriter, PackageMap, bTypicalSuccess));
    TestTrue(TEXT("典型事件序列化成功标记置位"), bTypicalSuccess);
    const int32 TypicalBytes = TypicalWriter.GetNumBytes();
    AddInfo(FString::Printf(TEXT("真实 PackageMap 典型事件载荷：%d 字节"), TypicalBytes));
    TestTrue(TEXT("典型网络载荷必须满足预算 <= 64 字节"), TypicalBytes <= 64);

    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackNetworkDefinitionContractTest,
    "Game.Feedback.NetworkDefinitionContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Dispatcher 的真实 RPC 出口及客户端 Receiver 的同版本身份闭环
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackNetworkDefinitionContractTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* Definition = CreateNetworkDefinitionAtPath(TEXT("/Game/Feedback/Network/Resolved.Resolved"));
    UGameFeedbackDefinition* OverrideDefinition = CreateNetworkDefinitionAtPath(TEXT("/Game/Feedback/Network/Override.Override"));
    const FGameplayTag ChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Network.Resolved")));
    const FGameplayTag OverrideTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Network.Override")));
    Registry->ExactChannelMap.Add(ChannelTag, Definition);
    Registry->ExactFeedbackTagMap.Add(OverrideTag, OverrideDefinition);
    Registry->RebuildDefinitionIndex();

    UGameFeedbackSettings* Settings = GetMutableDefault<UGameFeedbackSettings>();
    const TSoftObjectPtr<UGameFeedbackRegistry> PreviousRegistry = Settings->GlobalRegistry;
    Settings->GlobalRegistry = Registry;

    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackNetworkDefinitionContractTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("网络契约测试世界有效"), World);
    if (!World) {
        Settings->GlobalRegistry = PreviousRegistry;
        GameInstance->Shutdown();
        return false;
    }
    UGameFeedbackDefinitionProvider* Provider = World->GetSubsystem<UGameFeedbackDefinitionProvider>();
    TestNotNull(TEXT("Provider 必须可用于网络定义解析"), Provider);
    if (!Provider) {
        Settings->GlobalRegistry = PreviousRegistry;
        GameInstance->Shutdown();
        return false;
    }

    const uint32 DefinitionId = Provider->GetDefinitionId(Definition);
    const uint32 MappingVersion = Provider->GetDefinitionMappingVersion();
    TestEqual(TEXT("Provider 应返回服务端最终定义 ID"), DefinitionId, Registry->GetDefinitionId(Definition));
    TestEqual(TEXT("Provider 应返回 Registry 映射版本"), MappingVersion, Registry->GetDefinitionMappingVersion());
    TestTrue(TEXT("合法 ID 与版本应还原同一资产"), Provider->ResolveById(DefinitionId, MappingVersion) == Definition);
    TestNull(TEXT("未知 ID 必须拒绝解析"), Provider->ResolveById(0xFFFFFFFFu, MappingVersion));
    TestNull(TEXT("映射版本不一致必须拒绝解析"), Provider->ResolveById(DefinitionId, MappingVersion + 1));

    UGameplayMessageSubsystem* Bus = GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    int32 ReceivedCount = 0;
    FGameFeedbackNetworkMessage ReceivedMessage;
    FGameplayMessageListenerHandle Handle = Bus->RegisterListener<FGameFeedbackNetworkMessage>(
        TAG_Feedback_Channel_Network,
        [&ReceivedCount, &ReceivedMessage]([[maybe_unused]] FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message) {
            ++ReceivedCount;
            ReceivedMessage = Message;
        }
    );

    World->WorldType = EWorldType::PIE;
    World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
    FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World);
    UDemoNetDriver* Driver = NewObject<UDemoNetDriver>(GetTransientPackage());
    Driver->NetDriverName = NAME_GameNetDriver;
    Driver->World = World;
    USimulatedClientNetConnection* Connection = NewObject<USimulatedClientNetConnection>(Driver);
    Connection->Driver = Driver;
    Connection->SetInternalAck(true);
    WorldContext->ActiveNetDrivers.Add(FNamedNetDriver(Driver, nullptr));

    APlayerController* PC = World->SpawnActor<APlayerController>();
    PC->SetReplicates(true);
    PC->SetPlayer(Connection);
    World->AddController(PC);
    TestFalse(TEXT("Dispatcher 只能向远端控制器派发"), PC->IsLocalController());
    TestNotEqual(TEXT("远端控制器必须具备复制角色"), PC->GetRemoteRole(), ROLE_None);
    TestEqual(TEXT("测试世界必须能枚举到唯一远端控制器"), World->GetNumPlayerControllers(), 1);
    TestNotNull(TEXT("远端控制器必须绑定网络连接"), PC->GetNetConnection());
    AddInfo(FString::Printf(
        TEXT("远端控制器 NetMode=%d LocalRole=%d NetConnection=%s"),
        static_cast<int32>(PC->GetNetMode()),
        static_cast<int32>(PC->GetLocalRole()),
        PC->GetNetConnection() ? TEXT("valid") : TEXT("null")));
    UGameFeedbackReceiverComponent* Receiver = UGameFeedbackReceiverComponent::EnsureOnPlayerController(PC);
    AActor* DispatcherOwner = World->SpawnActor<AActor>();
    UGameFeedbackNetworkDispatcherComponent* Dispatcher = NewObject<UGameFeedbackNetworkDispatcherComponent>(DispatcherOwner);
    DispatcherOwner->AddInstanceComponent(Dispatcher);
    Dispatcher->RegisterComponent();
    Dispatcher->BeginPlay();

    TArray<FGameFeedbackEventNet> DispatchedEvents;
    Driver->SendRPCDel.BindLambda(
        [&DispatchedEvents, PC, Receiver](
            AActor* Actor,
            UFunction* Function,
            void* Parameters,
            FOutParmRec*,
            FFrame*,
            UObject* SubObject,
            bool& bOutBlockSendRPC) {
            if (Actor == PC && SubObject == Receiver
                && Function->GetFName() == GET_FUNCTION_NAME_CHECKED(UGameFeedbackReceiverComponent, ClientReceiveFeedback)) {
                const FStructProperty* EventProperty = CastField<FStructProperty>(Function->FindPropertyByName(TEXT("Event")));
                DispatchedEvents.Add(*EventProperty->ContainerPtrToValuePtr<FGameFeedbackEventNet>(Parameters));
                bOutBlockSendRPC = true;
            }
        }
    );

    FGameFeedbackSignal Signal;
    Signal.PrimaryChannel = ChannelTag;
    Signal.Magnitude = 10.0f;
    FGameFeedbackMessage RegularMessage;
    RegularMessage.Signal = Signal;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, RegularMessage);
    TestEqual(TEXT("常规 Dispatcher 应发送一次 Client RPC"), DispatchedEvents.Num(), 1);
    if (!DispatchedEvents.IsEmpty()) {
        TestEqual(TEXT("常规 Dispatcher 必须发送服务端 Definition ID"), DispatchedEvents[0].DefinitionId, DefinitionId);
        TestEqual(TEXT("常规 Dispatcher 必须发送映射版本"), DispatchedEvents[0].DefinitionMappingVersion, MappingVersion);
    }

    FGameFeedbackMessage OverrideMessage = RegularMessage;
    OverrideMessage.OverrideDefinition = OverrideDefinition;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, OverrideMessage);
    TestEqual(TEXT("Override Dispatcher 应继续发送 Client RPC"), DispatchedEvents.Num(), 2);
    if (DispatchedEvents.Num() >= 2) {
        TestEqual(TEXT("OverrideDefinition 必须优先于常规解析"), DispatchedEvents[1].DefinitionId, Registry->GetDefinitionId(OverrideDefinition));
    }

    FGameFeedbackMessage UnregisteredMessage = RegularMessage;
    UnregisteredMessage.OverrideDefinition = CreateNetworkDefinitionAtPath(TEXT("/Game/Feedback/Network/Unregistered.Unregistered"));
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, UnregisteredMessage);
    TestEqual(TEXT("未登记 Override 不得进行远端派发"), DispatchedEvents.Num(), 2);

    World->SetPlayInEditorInitialNetMode(NM_Client);
    World->NextURL = TEXT("127.0.0.1");
    TestEqual(TEXT("Receiver 入口必须在客户端 NetMode 执行"), World->GetNetMode(), NM_Client);
    const FGameFeedbackEventNet ValidEvent = FGameFeedbackEventNet::FromSignal(Signal, DefinitionId, MappingVersion);
    Receiver->ClientReceiveFeedback(ValidEvent);
    TestEqual(TEXT("合法 Receiver 事件应回灌一次"), ReceivedCount, 1);
    TestEqual(TEXT("Receiver 必须传递服务端已解析 Definition"), ReceivedMessage.ResolvedDefinition.Get(), Definition);

    FGameFeedbackEventNet UnknownEvent = ValidEvent;
    UnknownEvent.DefinitionId = 0xFFFFFFFFu;
    Receiver->ClientReceiveFeedback(UnknownEvent);
    TestEqual(TEXT("未知 Definition ID 不得回灌"), ReceivedCount, 1);

    FGameFeedbackEventNet StaleEvent = ValidEvent;
    StaleEvent.DefinitionMappingVersion = MappingVersion + 1;
    Receiver->ClientReceiveFeedback(StaleEvent);
    TestEqual(TEXT("过期映射版本不得回灌"), ReceivedCount, 1);

    Dispatcher->EndPlay(EEndPlayReason::Destroyed);
    Driver->SendRPCDel.Unbind();
    WorldContext->ActiveNetDrivers.RemoveAll([Driver](const FNamedNetDriver& Entry) {
        return Entry.NetDriver == Driver;
    });
    Handle.Unregister();
    Settings->GlobalRegistry = PreviousRegistry;
    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDedicatedServerServicesTest,
    "Game.Feedback.DedicatedServerServices",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 DS 可创建 Provider 与 HitStopService，且权威顿帧仍受配置门禁控制
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackDedicatedServerServicesTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* Definition = CreateNetworkDefinitionAtPath(TEXT("/Game/Feedback/Lifetime/DedicatedServer.DedicatedServer"));
    Definition->HitStop.bEnable = true;
    Definition->HitStop.VictimDuration = 0.08f;
    Definition->HitStop.AttackerDuration = 0.0f;
    const FGameplayTag ChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Lifetime.DedicatedServer")));
    Registry->ExactChannelMap.Add(ChannelTag, Definition);
    Registry->RebuildDefinitionIndex();

    UGameFeedbackSettings* Settings = GetMutableDefault<UGameFeedbackSettings>();
    const TSoftObjectPtr<UGameFeedbackRegistry> PreviousRegistry = Settings->GlobalRegistry;
    const bool bPreviousAllowAuthoritativeHitStop = Settings->bAllowAuthoritativeHitStop;
    Settings->GlobalRegistry = Registry;
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackDedicatedServerServicesTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("DS 生命周期测试世界有效"), World);
    if (!World) {
        Settings->GlobalRegistry = PreviousRegistry;
        Settings->bAllowAuthoritativeHitStop = bPreviousAllowAuthoritativeHitStop;
        GameInstance->Shutdown();
        return false;
    }

    World->WorldType = EWorldType::PIE;
    World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
    TestEqual(TEXT("测试世界必须模拟 Dedicated Server"), World->GetNetMode(), NM_DedicatedServer);
    TestTrue(TEXT("DS 必须允许创建 DefinitionProvider"), GetMutableDefault<UGameFeedbackDefinitionProvider>()->ShouldCreateSubsystem(World));
    TestTrue(TEXT("DS 必须允许创建 HitStopService"), GetMutableDefault<UGameFeedbackHitStopService>()->ShouldCreateSubsystem(World));

    UGameFeedbackDefinitionProvider* Provider = World->GetSubsystem<UGameFeedbackDefinitionProvider>();
    UGameFeedbackHitStopService* HitStopService = World->GetSubsystem<UGameFeedbackHitStopService>();
    UTimeDilationSubsystem* TimeDilation = World->GetSubsystem<UTimeDilationSubsystem>();
    UGameplayMessageSubsystem* Bus = GameInstance->GetSubsystem<UGameplayMessageSubsystem>();
    TestNotNull(TEXT("DS Provider 实例有效"), Provider);
    TestNotNull(TEXT("DS HitStopService 实例有效"), HitStopService);
    TestNotNull(TEXT("DS TimeDilationSubsystem 实例有效"), TimeDilation);
    TestNotNull(TEXT("DS 消息总线实例有效"), Bus);
    if (!Provider || !HitStopService || !TimeDilation || !Bus) {
        Settings->GlobalRegistry = PreviousRegistry;
        Settings->bAllowAuthoritativeHitStop = bPreviousAllowAuthoritativeHitStop;
        GameInstance->Shutdown();
        return false;
    }

    HitStopService->OnWorldBeginPlay(*World);
    AActor* Target = World->SpawnActor<AActor>();
    FGameFeedbackMessage Message;
    Message.Signal.PrimaryChannel = ChannelTag;
    Message.Signal.Target = Target;
    Message.Signal.Magnitude = 10.0f;

    Settings->bAllowAuthoritativeHitStop = false;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, Message);
    TestFalse(TEXT("默认配置禁止 DS 修改权威时间"), TimeDilation->IsActorInHitStop(Target));

    Settings->bAllowAuthoritativeHitStop = true;
    Bus->BroadcastMessage(TAG_Feedback_Channel_Play, Message);
    TestTrue(TEXT("允许配置下 DS 必须形成完整顿帧链路"), TimeDilation->IsActorInHitStop(Target));

    TimeDilation->ClearHitStop(Target);
    Settings->GlobalRegistry = PreviousRegistry;
    Settings->bAllowAuthoritativeHitStop = bPreviousAllowAuthoritativeHitStop;
    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
