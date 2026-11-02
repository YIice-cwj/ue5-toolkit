/**
 * @file GameFeedbackTests.cpp
 * @brief GameFeedback 自动化测试集合
 */

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Network/GameFeedbackEventNet.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Asset/GameFeedbackDefinition.h"
#include "Adapter/GameFeedbackAdapter.h"
#include "Bus/GameFeedbackChannels.h"
#include "Bus/GameFeedbackMessages.h"
#include "Component/GameFeedbackNetworkDispatcherComponent.h"
#include "Config/GameFeedbackSettings.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "System/GameFeedbackHitStopLimiter.h"
#include "System/GameFeedbackPredictionHistory.h"
#include "System/GameFeedbackVFXExecutor.h"
#include "Network/GameFeedbackReceiverComponent.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "GameplayTagsManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/PackageMapClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "System/GameFeedbackFlashTracker.h"
#include "System/GameFeedbackHitStopService.h"
#include "Time/TimeDilationSubsystem.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_AUTOMATION_TESTS

namespace {

/**
 * @brief 创建具有稳定资产路径的测试 Definition
 * @param ObjectPath 完整对象路径（Package.Object）
 * @return 测试 Definition
 */
UGameFeedbackDefinition* CreateDefinitionAtPath(const FString& ObjectPath)
{
    FString PackageName;
    FString ObjectName;
    ObjectPath.Split(TEXT("."), &PackageName, &ObjectName);
    return NewObject<UGameFeedbackDefinition>(CreatePackage(*PackageName), *ObjectName);
}

}

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
    FGameFeedbackRegistryLookupTest,
    "Game.Feedback.RegistryLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Registry 按真实 GameplayTag 层级择优并按同深度字典序稳定决策
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackRegistryLookupTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* DefaultDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* SlashDef = NewObject<UGameFeedbackDefinition>();

    Registry->DefaultFallbackDefinition = DefaultDef;

    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Physical.Slash")));
    const FGameplayTag FireTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Elemental.Fire")));

    Registry->ExactChannelMap.Add(SlashTag, SlashDef);

    UGameFeedbackDefinition* ShallowLongDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* DeepShortDef = NewObject<UGameFeedbackDefinition>();
    const FGameplayTag ShallowLongTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.ThisIsAVeryLongShallowTag")));
    const FGameplayTag DeepShortTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.A.B.C")));
    Registry->ExactFeedbackTagMap.Add(ShallowLongTag, ShallowLongDef);
    Registry->ExactFeedbackTagMap.Add(DeepShortTag, DeepShortDef);

    FGameplayTagContainer Tags;
    TSoftObjectPtr<UGameFeedbackDefinition> FoundSlash = Registry->ResolveDefinitionPtr(SlashTag, Tags);
    TestEqual(TEXT("精确伤害通道应命中 Slash 资产"), FoundSlash.Get(), SlashDef);

    TSoftObjectPtr<UGameFeedbackDefinition> FoundFallback = Registry->ResolveDefinitionPtr(FireTag, Tags);
    TestEqual(TEXT("未匹配通道应退化回 DefaultFallbackDefinition"), FoundFallback.Get(), DefaultDef);

    Tags.AddTag(ShallowLongTag);
    Tags.AddTag(DeepShortTag);
    TSoftObjectPtr<UGameFeedbackDefinition> FoundTag = Registry->ResolveDefinitionPtr(FireTag, Tags);
    TestEqual(TEXT("真实层级更深者优先于字符串长度"), FoundTag.Get(), DeepShortDef);

    Registry->ExactFeedbackTagMap.Reset();
    UGameFeedbackDefinition* AlphaDef = NewObject<UGameFeedbackDefinition>();
    UGameFeedbackDefinition* BetaDef = NewObject<UGameFeedbackDefinition>();
    const FGameplayTag AlphaTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Tie.Alpha")));
    const FGameplayTag BetaTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Tie.Beta")));
    Registry->ExactFeedbackTagMap.Add(AlphaTag, AlphaDef);
    Registry->ExactFeedbackTagMap.Add(BetaTag, BetaDef);
    Tags.Reset();
    Tags.AddTag(AlphaTag);
    Tags.AddTag(BetaTag);
    FoundTag = Registry->ResolveDefinitionPtr(FireTag, Tags);
    TestEqual(TEXT("同深度按标签字典序稳定选择"), FoundTag.Get(), AlphaDef);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDefinitionIdentityTest,
    "Game.Feedback.DefinitionIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Definition 稳定 ID、映射版本、全部来源索引及 CRC 冲突拒绝
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackDefinitionIdentityTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackRegistry* EquivalentRegistry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* DefaultDef = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Default.Default"));
    UGameFeedbackDefinition* ChannelDef = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Channel.Channel"));
    UGameFeedbackDefinition* TagDef = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Tag.Tag"));
    UGameFeedbackDefinition* CompositeDef = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Composite.Composite"));
    const FGameplayTag ChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Identity.Channel")));
    const FGameplayTag FeedbackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Identity.Tag")));

    for (UGameFeedbackRegistry* CurrentRegistry : {Registry, EquivalentRegistry}) {
        CurrentRegistry->DefaultFallbackDefinition = DefaultDef;
        CurrentRegistry->ExactChannelMap.Add(ChannelTag, ChannelDef);
        CurrentRegistry->ExactFeedbackTagMap.Add(FeedbackTag, TagDef);
        FGameFeedbackCompositeEntry Entry;
        Entry.Priority = 7;
        Entry.Definition = CompositeDef;
        CurrentRegistry->CompositeRules.Add(Entry);
        CurrentRegistry->RebuildDefinitionIndex();
    }

    const uint32 InitialVersion = Registry->GetDefinitionMappingVersion();
    TestNotEqual(TEXT("有效 Registry 必须具有非零映射版本"), InitialVersion, 0u);
    TestEqual(TEXT("相同映射生成相同版本"), EquivalentRegistry->GetDefinitionMappingVersion(), InitialVersion);

    for (UGameFeedbackDefinition* Definition : {DefaultDef, ChannelDef, TagDef, CompositeDef}) {
        const uint32 DefinitionId = Registry->GetDefinitionId(Definition);
        TestNotEqual(TEXT("已登记 Definition 必须具有非零 ID"), DefinitionId, 0u);
        TestEqual(TEXT("同路径在等价 Registry 中 ID 确定"), EquivalentRegistry->GetDefinitionId(Definition), DefinitionId);
        TestEqual(TEXT("全部 Registry 来源均可按 ID 往返"), Registry->FindDefinitionById(DefinitionId).Get(), Definition);
    }

    Registry->ExactChannelMap.FindChecked(ChannelTag) = CompositeDef;
    Registry->RebuildDefinitionIndex();
    TestNotEqual(TEXT("映射或资产路径变化必须改变版本"), Registry->GetDefinitionMappingVersion(), InitialVersion);

    UGameFeedbackDefinition* Unregistered = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Unregistered.Unregistered"));
    TestEqual(TEXT("未登记 Override 不可生成网络 ID"), Registry->GetDefinitionId(Unregistered), 0u);
    TestTrue(TEXT("未知 ID 不可解析"), Registry->FindDefinitionById(0xFFFFFFFFu).IsNull());

    UGameFeedbackRegistry* CollisionRegistry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* CollisionA = CreateDefinitionAtPath(TEXT("/Game/Feedback/Collision/Def_8733.Def_8733"));
    UGameFeedbackDefinition* CollisionB = CreateDefinitionAtPath(TEXT("/Game/Feedback/Collision/Def_54027.Def_54027"));
    const FGameplayTag CollisionTagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Collision.A")));
    const FGameplayTag CollisionTagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Collision.B")));
    CollisionRegistry->ExactFeedbackTagMap.Add(CollisionTagA, CollisionA);
    CollisionRegistry->ExactFeedbackTagMap.Add(CollisionTagB, CollisionB);
    CollisionRegistry->RebuildDefinitionIndex();
    const uint32 CollisionIdA = UGameFeedbackRegistry::MakeDefinitionId(FSoftObjectPath(CollisionA));
    const uint32 CollisionIdB = UGameFeedbackRegistry::MakeDefinitionId(FSoftObjectPath(CollisionB));
    TestNotEqual(TEXT("CRC 冲突夹具必须生成有效 ID"), CollisionIdA, 0u);
    TestEqual(TEXT("CRC 冲突夹具必须有效"), CollisionIdA, CollisionIdB);
    TestEqual(TEXT("CRC 冲突项 A 必须不可解析"), CollisionRegistry->GetDefinitionId(CollisionA), 0u);
    TestEqual(TEXT("CRC 冲突项 B 必须不可解析"), CollisionRegistry->GetDefinitionId(CollisionB), 0u);

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
    UGameFeedbackDefinition* Definition = CreateDefinitionAtPath(TEXT("/Game/Feedback/Network/Resolved.Resolved"));
    UGameFeedbackDefinition* OverrideDefinition = CreateDefinitionAtPath(TEXT("/Game/Feedback/Network/Override.Override"));
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
    UnregisteredMessage.OverrideDefinition = CreateDefinitionAtPath(TEXT("/Game/Feedback/Network/Unregistered.Unregistered"));
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
    FGameFeedbackHitStopLimiterTest,
    "Game.Feedback.HitStopLimiter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameFeedbackHitStopLimiterTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    AActor* Attacker = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    FGameFeedbackHitStopAction Action;
    Action.CooldownInterval = 0.05f;
    Action.MaxCumulativeDuration = 0.20f;
    Action.ResetWindow = 0.50f;
    Action.VictimDuration = 0.08f;

    FGameFeedbackHitStopLimiter Limiter;
    TestTrue(TEXT("首次命中应通过冷却"), Limiter.PassCooldown(Attacker, Target, Action, 0.0));
    TestFalse(TEXT("冷却窗口内再次命中应被拒绝"), Limiter.PassCooldown(Attacker, Target, Action, 0.02));
    TestTrue(TEXT("冷却结束后应重新放行"), Limiter.PassCooldown(Attacker, Target, Action, 0.10));

    TestTrue(TEXT("首次申请应获得完整时长"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.01), 0.08f));
    TestTrue(TEXT("第二次申请应获得完整时长"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.04), 0.08f));
    TestTrue(TEXT("第三次申请应被累积预算截断"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.08), 0.04f));
    TestTrue(TEXT("预算耗尽后应返回零"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.12), 0.0f));
    TestTrue(TEXT("越过重置窗口后累积预算应清零"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.90), 0.08f));

    Limiter.ClearActor(Target);
    TestTrue(TEXT("清除后应重新获得完整预算"), FMath::IsNearlyEqual(Limiter.AcquireDuration(Target, 0.08f, Action, 0.95), 0.08f));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackPredictionHistoryTest,
    "Game.Feedback.PredictionHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证预测事件仅按非零 ID 与双实体匹配，并覆盖乱序、丢失、重复与过期
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackPredictionHistoryTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    AActor* Attacker = NewObject<AActor>(GetTransientPackage());
    AActor* OtherAttacker = NewObject<AActor>(GetTransientPackage());
    AActor* Target = NewObject<AActor>(GetTransientPackage());

    FGameFeedbackPredictionHistory History;
    History.Add(7, Attacker, Target, 1.0);
    TestFalse(TEXT("同实体不同 ID 不得误消费"), History.ConsumeMatch(8, Attacker, Target, 1.1));
    TestFalse(TEXT("实体条件仍须匹配"), History.ConsumeMatch(7, OtherAttacker, Target, 1.1));
    TestTrue(TEXT("有效窗口内应消费相同事件 ID"), History.ConsumeMatch(7, Attacker, Target, 1.1));
    TestFalse(TEXT("重复回执不得二次消费"), History.ConsumeMatch(7, Attacker, Target, 1.2));

    History.Add(7, Attacker, Target, 2.0);
    History.Add(8, Attacker, Target, 2.0);
    History.Add(9, Attacker, Target, 2.0);
    TestTrue(TEXT("乱序回执 ID 9 应独立消费"), History.ConsumeMatch(9, Attacker, Target, 2.1));
    TestTrue(TEXT("ID 7 回执丢失不影响 ID 8"), History.ConsumeMatch(8, Attacker, Target, 2.1));
    TestFalse(TEXT("未预测事件不得被旧记录误抑制"), History.ConsumeMatch(10, Attacker, Target, 2.1));
    TestTrue(TEXT("迟到的 ID 7 仍可准确消费"), History.ConsumeMatch(7, Attacker, Target, 2.2));

    History.Add(0, Attacker, Target, 3.0);
    TestFalse(TEXT("ID 0 永不记录或消费预测"), History.ConsumeMatch(0, Attacker, Target, 3.1));

    History.Add(11, Attacker, Target, 4.0);
    TestFalse(TEXT("超出 0.5 秒窗口的指纹应被清理"), History.ConsumeMatch(11, Attacker, Target, 4.6));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackAdapterTest,
    "Game.Feedback.AdapterConversion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameFeedbackAdapterTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    FGameDamageContext Context;
    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Channel.Slash")));
    const FGameplayTag BluntTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Channel.Blunt")));

    Context.HitResult.ImpactNormal = FVector::UpVector;
    Context.HitResult.ImpactPoint = FVector(10.0f, 20.0f, 30.0f);
    Context.HitResult.TraceStart = FVector::ZeroVector;
    Context.HitResult.TraceEnd = FVector(10.0f, 0.0f, 0.0f);

    FGameDamageResult Result;
    Result.SetActualDamage(150.0f);
    FGameDamageChannel::SetExact(Result.ActualChannels, SlashTag, 100.0f);
    FGameDamageChannel::SetExact(Result.ActualChannels, BluntTag, 50.0f);
    Result.AddFeedbackTag(TAG_Feedback_State_Lethal);

    FGameFeedbackSignal Signal = UGameFeedbackAdapter::MakeSignalFromDamage(nullptr, Context, Result);

    TestEqual(TEXT("主导通道应选择伤害最大的通道 (Slash)"), Signal.PrimaryChannel, SlashTag);
    TestEqual(TEXT("伤害总值应当正确传递"), Signal.Magnitude, 150.0f);
    TestTrue(TEXT("包含致死标签时 bLethal 必须为 true"), Signal.bLethal);
    TestEqual(TEXT("冲击法线应正确对齐"), Signal.HitResult.ImpactNormal, FVector::UpVector);
    TestEqual(TEXT("轨迹方向应显式保留"), Signal.TraceDirection, FVector::ForwardVector);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackVFXTraceOrientationTest,
    "Game.Feedback.VFXTraceOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证网络缺失 Trace 端点时 VFX 仍按显式轨迹方向计算朝向
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackVFXTraceOrientationTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    FGameFeedbackSignal Signal;
    Signal.TraceDirection = FVector::ForwardVector;
    Signal.HitResult.ImpactNormal = FVector::UpVector;
    Signal.HitResult.TraceStart = FVector::ZeroVector;
    Signal.HitResult.TraceEnd = FVector::ZeroVector;

    const FRotator OppositeRotation = GameFeedback::Execution::ResolveVFXRotation(
        Signal, EGameFeedbackVFXOrientation::OppositeToTraceDirection);
    TestEqual(TEXT("反向轨迹模式必须使用显式 TraceDirection"), OppositeRotation.Vector(), -FVector::ForwardVector);

    const FRotator ReflectedRotation = GameFeedback::Execution::ResolveVFXRotation(
        Signal, EGameFeedbackVFXOrientation::AlignToReflectedNormal);
    const FVector ExpectedReflection = FMath::GetReflectionVector(FVector::ForwardVector, FVector::UpVector);
    TestTrue(TEXT("反射法线模式必须使用显式 TraceDirection"), ReflectedRotation.Vector().Equals(ExpectedReflection, KINDA_SMALL_NUMBER));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackFlashTrackerLifecycleTest,
    "Game.Feedback.FlashTrackerLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证闪白按最大绝对到期续期、提前回调重新 arm，并在 GC 后安全还原
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackFlashTrackerLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FTimerManager& TimerManager = World->GetTimerManager();
    FGameFeedbackFlashTracker Tracker;

    AActor* MaxTarget = World->SpawnActor<AActor>();
    UStaticMeshComponent* MaxMesh = NewObject<UStaticMeshComponent>(MaxTarget);
    MaxTarget->SetRootComponent(MaxMesh);
    MaxMesh->RegisterComponent();
    UMaterial* OriginalMaterial = NewObject<UMaterial>(GetTransientPackage());
    UMaterial* FlashMaterial = NewObject<UMaterial>(GetTransientPackage());
    MaxMesh->SetOverlayMaterial(OriginalMaterial);

    FGameFeedbackHitFlashAction Action;
    Action.TargetMode = EGameFeedbackFlashTargetMode::HitComponentOnly;
    Action.OverlayMaterial = FlashMaterial;
    Action.Duration = 10.0f;
    FGameFeedbackHitFlashAction ShorterAction = Action;
    ShorterAction.Duration = 0.001f;
    FHitResult HitResult;
    HitResult.Component = MaxMesh;

    const double MaxStartRealTime = FPlatformTime::Seconds();
    Tracker.Execute(World, MaxTarget, HitResult, Action, 10.0f);
    Tracker.Execute(World, MaxTarget, HitResult, ShorterAction, 10.0f);
    TWeakObjectPtr<UMaterialInterface> OriginalWeak = OriginalMaterial;
    OriginalMaterial = nullptr;
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("仅由 Tracker 持有的原 Overlay 必须经 GC 存活"), OriginalWeak.IsValid());
    TestTrue(TEXT("最大到期用例必须越过较短 Duration"), FPlatformTime::Seconds() >= MaxStartRealTime + ShorterAction.Duration);
    TestTrue(TEXT("提前回调必须发生在最大绝对到期之前"), FPlatformTime::Seconds() < MaxStartRealTime + Action.Duration);

    ++GFrameCounter;
    TimerManager.Tick(0.0f);
    ++GFrameCounter;
    TimerManager.Tick(2.0f);
    TestTrue(TEXT("较短 Duration 不得覆盖最大绝对到期"), MaxMesh->GetOverlayMaterial() == FlashMaterial);

    Tracker.ClearActor(World, MaxTarget);
    TestEqual(TEXT("ClearActor 必须还原 GC 后仍存活的原 Overlay"), MaxMesh->GetOverlayMaterial(), OriginalWeak.Get());
    Tracker.Execute(World, MaxTarget, HitResult, Action, 10.0f);
    Tracker.Shutdown(World);
    TestEqual(TEXT("Shutdown 必须还原 Overlay"), MaxMesh->GetOverlayMaterial(), OriginalWeak.Get());

    AActor* RearmTarget = World->SpawnActor<AActor>();
    UStaticMeshComponent* RearmMesh = NewObject<UStaticMeshComponent>(RearmTarget);
    RearmTarget->SetRootComponent(RearmMesh);
    RearmMesh->RegisterComponent();
    HitResult.Component = RearmMesh;
    Action.Duration = 0.05f;
    ShorterAction.Duration = 0.001f;
    const double RearmExpirationRealTime = FPlatformTime::Seconds() + Action.Duration;
    Tracker.Execute(World, RearmTarget, HitResult, Action, 10.0f);
    Tracker.Execute(World, RearmTarget, HitResult, ShorterAction, 10.0f);
    TestTrue(TEXT("重新 arm 用例的首次回调必须提前发生"), FPlatformTime::Seconds() < RearmExpirationRealTime);

    ++GFrameCounter;
    TimerManager.Tick(0.0f);
    ++GFrameCounter;
    TimerManager.Tick(1.0f);
    TestTrue(TEXT("提前回调不得提前还原 Overlay"), RearmMesh->GetOverlayMaterial() == FlashMaterial);
    while (FPlatformTime::Seconds() < RearmExpirationRealTime) {
    }
    TestTrue(TEXT("最终回调前必须已经越过真实到期时间"), FPlatformTime::Seconds() >= RearmExpirationRealTime);
    ++GFrameCounter;
    TimerManager.Tick(1.0f);
    TestNull(TEXT("提前回调后重新 arm 必须在最终到期时还原"), RearmMesh->GetOverlayMaterial());

    Tracker.Shutdown(World);
    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDefinitionProviderLifetimeTest,
    "Game.Feedback.DefinitionProviderLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Definition 强缓存、同路径异步句柄去重、加载完成后再解析缓存及注销释放
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackDefinitionProviderLifetimeTest::RunTest([[maybe_unused]] const FString& Parameters)
{
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* CachedDefinition = CreateDefinitionAtPath(TEXT("/Game/Feedback/Lifetime/Cached.Cached"));
    const FGameplayTag CachedChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Lifetime.Cached")));
    Registry->ExactChannelMap.Add(CachedChannel, CachedDefinition);
    Registry->RebuildDefinitionIndex();

    UGameFeedbackSettings* Settings = GetMutableDefault<UGameFeedbackSettings>();
    const TSoftObjectPtr<UGameFeedbackRegistry> PreviousRegistry = Settings->GlobalRegistry;
    Settings->GlobalRegistry = Registry;
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackDefinitionProviderLifetimeTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    UGameFeedbackDefinitionProvider* Provider = World ? World->GetSubsystem<UGameFeedbackDefinitionProvider>() : nullptr;
    TestNotNull(TEXT("Provider 生命周期测试世界有效"), World);
    TestNotNull(TEXT("Provider 生命周期服务有效"), Provider);
    if (!World || !Provider) {
        Settings->GlobalRegistry = PreviousRegistry;
        GameInstance->Shutdown();
        return false;
    }

    FGameFeedbackSignal CachedSignal;
    CachedSignal.PrimaryChannel = CachedChannel;
    TestTrue(TEXT("首次 Resolve 必须返回已加载 Definition"), Provider->Resolve(CachedSignal) == CachedDefinition);
    TWeakObjectPtr<UGameFeedbackDefinition> CachedWeak = CachedDefinition;
    CachedDefinition = nullptr;
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("已解析 Definition 必须由 Provider 强缓存"), CachedWeak.IsValid());

    const FString AsyncPackageName = TEXT("/Game/GameFeedbackProviderLifetimeAsync");
    const FString AsyncFileName = FPackageName::LongPackageNameToFilename(AsyncPackageName, FPackageName::GetAssetPackageExtension());
    const bool bAsyncAssetAlreadyExists = IFileManager::Get().FileExists(*AsyncFileName);
    TestFalse(TEXT("异步测试不得覆盖已有资产"), bAsyncAssetAlreadyExists);
    if (bAsyncAssetAlreadyExists) {
        Settings->GlobalRegistry = PreviousRegistry;
        GameInstance->Shutdown();
        return false;
    }

    UGameFeedbackDefinition* AsyncDefinition = CreateDefinitionAtPath(AsyncPackageName + TEXT(".GameFeedbackProviderLifetimeAsync"));
    AsyncDefinition->SetFlags(RF_Public | RF_Standalone);
    UPackage* AsyncPackage = AsyncDefinition->GetOutermost();
    AsyncPackage->MarkAsFullyLoaded();
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bAsyncAssetSaved = UPackage::SavePackage(AsyncPackage, AsyncDefinition, *AsyncFileName, SaveArgs);
    TestTrue(TEXT("异步测试资产必须真实保存"), bAsyncAssetSaved);
    if (!bAsyncAssetSaved) {
        Settings->GlobalRegistry = PreviousRegistry;
        GameInstance->Shutdown();
        return false;
    }
    ON_SCOPE_EXIT {
        IFileManager::Get().Delete(*AsyncFileName);
    };

    const FSoftObjectPath AsyncPath = AsyncDefinition->GetPathName();
    const TSoftObjectPtr<UGameFeedbackDefinition> AsyncSoft(AsyncPath);
    const FGameplayTag AsyncChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Lifetime.Async")));
    Registry->ExactChannelMap.Add(AsyncChannel, AsyncSoft);
    Registry->RebuildDefinitionIndex();
    FGameFeedbackSignal AsyncSignal;
    AsyncSignal.PrimaryChannel = AsyncChannel;

    TWeakObjectPtr<UGameFeedbackDefinition> AsyncWeak = AsyncDefinition;
    AsyncDefinition->ClearFlags(RF_Standalone);
    AsyncPackage->ClearFlags(RF_Standalone);
    AsyncDefinition = nullptr;
    AsyncPackage = nullptr;
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("异步测试资产必须在 Resolve 前真实卸载"), AsyncWeak.IsValid());

    Provider->Resolve(AsyncSignal);
    Provider->Resolve(AsyncSignal);

    FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
    TArray<TSharedRef<FStreamableHandle>> Handles;
    StreamableManager.GetActiveHandles(AsyncPath, Handles);
    TestEqual(TEXT("同一路径连续 Resolve 只能保留一个异步句柄"), Handles.Num(), 1);
    if (!Handles.IsEmpty()) {
        Handles[0]->WaitUntilComplete();
        TestTrue(TEXT("异步 Definition 加载必须真实完成"), Handles[0]->HasLoadCompleted());
        AsyncWeak = Handles[0]->GetLoadedAsset<UGameFeedbackDefinition>();
        TestTrue(
            TEXT("加载完成后再次 Resolve 必须返回同一已加载 Definition"),
            AsyncWeak.IsValid() && Provider->Resolve(AsyncSignal) == AsyncWeak.Get());
        Handles[0]->ReleaseHandle();
    }

    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("加载完成后 Resolve 必须写入独立保活的强缓存"), AsyncWeak.IsValid());

    Settings->GlobalRegistry = PreviousRegistry;
    World->DestroyWorld(true);
    GameInstance->Shutdown();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Deinitialize 必须释放同步解析缓存"), CachedWeak.IsValid());
    TestFalse(TEXT("Deinitialize 必须释放异步完成缓存"), AsyncWeak.IsValid());
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
    UGameFeedbackDefinition* Definition = CreateDefinitionAtPath(TEXT("/Game/Feedback/Lifetime/DedicatedServer.DedicatedServer"));
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

#endif
