// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineSessionRoomAdapter.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

const FName FOnlineSessionRoomAdapter::DEFAULT_SESSION_NAME(TEXT("GameNetSession"));
const FName FOnlineSessionRoomAdapter::SETTING_KEY_ROOM_NAME(TEXT("GAMENET_ROOM_NAME"));
const FName FOnlineSessionRoomAdapter::SETTING_KEY_MAP_PATH(TEXT("GAMENET_MAP_PATH"));
const FName FOnlineSessionRoomAdapter::SETTING_KEY_GAME_PORT(TEXT("GAMENET_GAME_PORT"));
const FName FOnlineSessionRoomAdapter::OP_CREATE_ROOM(TEXT("CreateRoom"));
const FName FOnlineSessionRoomAdapter::OP_FIND_ROOMS(TEXT("FindRooms"));
const FName FOnlineSessionRoomAdapter::OP_JOIN_ROOM(TEXT("JoinRoom"));
const FName FOnlineSessionRoomAdapter::OP_DESTROY_ROOM(TEXT("DestroyRoom"));

namespace {

/**
 * @brief 安全解析指定世界对应的 OnlineSession 接口指针
 * @param WorldContext 世界上下文指针
 * @return OnlineSession 接口共享指针，未启用 OSS 时返回 nullptr
 */
IOnlineSessionPtr ResolveOnlineSessionInterface(const UWorld* WorldContext) {
    if (WorldContext != nullptr && GEngine != nullptr && GEngine->GetWorldContextFromWorld(WorldContext) != nullptr) {
        return Online::GetSessionInterface(WorldContext);
    }

    IOnlineSubsystem* DefaultOss = IOnlineSubsystem::Get();
    return DefaultOss != nullptr ? DefaultOss->GetSessionInterface() : nullptr;
}

}

/**
 * @brief 析构时自动解绑全部 OnlineSession 异步回调句柄并清空搜索缓存
 */
FOnlineSessionRoomAdapter::~FOnlineSessionRoomAdapter() {
    Shutdown(nullptr);
}

/**
 * @brief 发起异步创建平台在线房间请求
 * @param WorldContext 当前世界上下文指针
 * @param Request 房间创建描述参数
 * @param LocalUserNum 本地玩家控制器索引
 * @return 异步流程成功发起返回 true
 */
bool FOnlineSessionRoomAdapter::CreateRoom(
    const UWorld* WorldContext,
    const FGameNetCreateRoomRequest& Request,
    int32 LocalUserNum
) {
    if (Request.RoomName.IsEmpty() || Request.MaxPlayers <= 0 || LocalUserNum < 0) {
        return false;
    }

    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext);
    if (!SessionInt.IsValid()) {
        return false;
    }

    if (SessionInt->GetNamedSession(DEFAULT_SESSION_NAME) != nullptr) {
        OperationCompletedDelegate.Broadcast(OP_CREATE_ROOM, false, TEXT("SessionAlreadyExists"));
        return false;
    }

    FOnlineSessionSettings Settings;
    Settings.NumPublicConnections = Request.MaxPlayers;
    Settings.bShouldAdvertise = true;
    Settings.bAllowJoinInProgress = Request.bAllowJoinInProgress;
    Settings.bIsLANMatch = Request.bIsLanMatch;
    Settings.bUsesPresence = !Request.bIsLanMatch;
    Settings.Set(SETTING_KEY_ROOM_NAME, Request.RoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(SETTING_KEY_MAP_PATH, Request.MapPath, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(SETTING_KEY_GAME_PORT, Request.HostGamePort, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    if (CreateDelegateHandle.IsValid()) {
        SessionInt->ClearOnCreateSessionCompleteDelegate_Handle(CreateDelegateHandle);
    }
    const TWeakObjectPtr<const UWorld> WeakWorld(WorldContext);
    CreateDelegateHandle = SessionInt->AddOnCreateSessionCompleteDelegate_Handle(
        FOnCreateSessionCompleteDelegate::CreateLambda([this, WeakWorld](FName SessionName, bool bWasSuccessful) {
            HandleCreateComplete(WeakWorld, SessionName, bWasSuccessful);
        })
    );

    if (!SessionInt->CreateSession(LocalUserNum, DEFAULT_SESSION_NAME, Settings)) {
        SessionInt->ClearOnCreateSessionCompleteDelegate_Handle(CreateDelegateHandle);
        CreateDelegateHandle.Reset();
        return false;
    }

    return true;
}

/**
 * @brief 发起异步搜索平台在线房间列表请求
 * @param WorldContext 当前世界上下文指针
 * @param bLanQuery 是否仅搜索局域网会话
 * @param MaxResults 最大返回房间条数
 * @param LocalUserNum 本地玩家控制器索引
 * @return 异步流程成功发起返回 true
 */
bool FOnlineSessionRoomAdapter::FindRooms(
    const UWorld* WorldContext,
    bool bLanQuery,
    int32 MaxResults,
    int32 LocalUserNum
) {
    if (MaxResults <= 0 || LocalUserNum < 0) {
        return false;
    }

    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext);
    if (!SessionInt.IsValid()) {
        return false;
    }

    TSharedRef<FOnlineSessionSearch> Search = MakeShared<FOnlineSessionSearch>();
    Search->bIsLanQuery = bLanQuery;
    Search->MaxSearchResults = MaxResults;
    ActiveSessionSearch = Search;

    if (FindDelegateHandle.IsValid()) {
        SessionInt->ClearOnFindSessionsCompleteDelegate_Handle(FindDelegateHandle);
    }
    const TWeakObjectPtr<const UWorld> WeakWorld(WorldContext);
    FindDelegateHandle = SessionInt->AddOnFindSessionsCompleteDelegate_Handle(
        FOnFindSessionsCompleteDelegate::CreateLambda([this, WeakWorld](bool bWasSuccessful) {
            HandleFindComplete(WeakWorld, bWasSuccessful);
        })
    );

    if (!SessionInt->FindSessions(LocalUserNum, Search)) {
        SessionInt->ClearOnFindSessionsCompleteDelegate_Handle(FindDelegateHandle);
        FindDelegateHandle.Reset();
        ActiveSessionSearch.Reset();
        return false;
    }

    return true;
}

/**
 * @brief 按最近一次搜索结果数组下标发起异步加入指定房间请求
 * @param WorldContext 当前世界上下文指针
 * @param SearchResultIndex 搜索结果下标
 * @param LocalUserNum 本地玩家控制器索引
 * @return 异步流程成功发起返回 true
 */
bool FOnlineSessionRoomAdapter::JoinRoomByIndex(
    const UWorld* WorldContext,
    int32 SearchResultIndex,
    int32 LocalUserNum
) {
    if (!ActiveSessionSearch.IsValid() || !ActiveSessionSearch->SearchResults.IsValidIndex(SearchResultIndex) || LocalUserNum < 0) {
        return false;
    }

    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext);
    if (!SessionInt.IsValid()) {
        return false;
    }

    if (JoinDelegateHandle.IsValid()) {
        SessionInt->ClearOnJoinSessionCompleteDelegate_Handle(JoinDelegateHandle);
    }
    const TWeakObjectPtr<const UWorld> WeakWorld(WorldContext);
    JoinDelegateHandle = SessionInt->AddOnJoinSessionCompleteDelegate_Handle(
        FOnJoinSessionCompleteDelegate::CreateLambda(
            [this, WeakWorld](FName SessionName, EOnJoinSessionCompleteResult::Type Result) {
                HandleJoinComplete(WeakWorld, SessionName, Result == EOnJoinSessionCompleteResult::Success);
            }
        )
    );

    if (!SessionInt->JoinSession(LocalUserNum, DEFAULT_SESSION_NAME, ActiveSessionSearch->SearchResults[SearchResultIndex])) {
        SessionInt->ClearOnJoinSessionCompleteDelegate_Handle(JoinDelegateHandle);
        JoinDelegateHandle.Reset();
        return false;
    }

    return true;
}

/**
 * @brief 发起异步销毁当前命名平台会话请求
 * @param WorldContext 当前世界上下文指针
 * @return 异步流程成功发起返回 true
 */
bool FOnlineSessionRoomAdapter::DestroyRoom(const UWorld* WorldContext) {
    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext);
    if (!SessionInt.IsValid() || SessionInt->GetNamedSession(DEFAULT_SESSION_NAME) == nullptr) {
        return false;
    }

    if (DestroyDelegateHandle.IsValid()) {
        SessionInt->ClearOnDestroySessionCompleteDelegate_Handle(DestroyDelegateHandle);
    }
    const TWeakObjectPtr<const UWorld> WeakWorld(WorldContext);
    DestroyDelegateHandle = SessionInt->AddOnDestroySessionCompleteDelegate_Handle(
        FOnDestroySessionCompleteDelegate::CreateLambda([this, WeakWorld](FName SessionName, bool bWasSuccessful) {
            HandleDestroyComplete(WeakWorld, SessionName, bWasSuccessful);
        })
    );

    if (!SessionInt->DestroySession(DEFAULT_SESSION_NAME)) {
        SessionInt->ClearOnDestroySessionCompleteDelegate_Handle(DestroyDelegateHandle);
        DestroyDelegateHandle.Reset();
        return false;
    }

    return true;
}

/**
 * @brief 解绑所有未决平台会话回调句柄并重置搜索结果缓存
 * @param WorldContext 当前世界上下文指针
 */
void FOnlineSessionRoomAdapter::Shutdown(const UWorld* WorldContext) {
    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext);
    if (SessionInt.IsValid()) {
        if (CreateDelegateHandle.IsValid()) {
            SessionInt->ClearOnCreateSessionCompleteDelegate_Handle(CreateDelegateHandle);
        }
        if (FindDelegateHandle.IsValid()) {
            SessionInt->ClearOnFindSessionsCompleteDelegate_Handle(FindDelegateHandle);
        }
        if (JoinDelegateHandle.IsValid()) {
            SessionInt->ClearOnJoinSessionCompleteDelegate_Handle(JoinDelegateHandle);
        }
        if (DestroyDelegateHandle.IsValid()) {
            SessionInt->ClearOnDestroySessionCompleteDelegate_Handle(DestroyDelegateHandle);
        }
    }

    CreateDelegateHandle.Reset();
    FindDelegateHandle.Reset();
    JoinDelegateHandle.Reset();
    DestroyDelegateHandle.Reset();
    CachedRooms.Empty();
    ActiveSessionSearch.Reset();
}

/**
 * @brief 处理平台创建房间异步完成回调
 * @param WorldContext 世界上下文弱指针
 * @param SessionName 会话名
 * @param bWasSuccessful 是否成功
 */
void FOnlineSessionRoomAdapter::HandleCreateComplete(
    TWeakObjectPtr<const UWorld> WorldContext,
    FName SessionName,
    bool bWasSuccessful
) {
    if (SessionName != DEFAULT_SESSION_NAME) {
        return;
    }

    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext.Get());
    if (SessionInt.IsValid() && CreateDelegateHandle.IsValid()) {
        SessionInt->ClearOnCreateSessionCompleteDelegate_Handle(CreateDelegateHandle);
        CreateDelegateHandle.Reset();
    }

    OperationCompletedDelegate.Broadcast(OP_CREATE_ROOM, bWasSuccessful, FString());
}

/**
 * @brief 处理平台搜索房间异步完成回调并构建标准化描述符缓存
 * @param WorldContext 世界上下文弱指针
 * @param bWasSuccessful 是否成功
 */
void FOnlineSessionRoomAdapter::HandleFindComplete(TWeakObjectPtr<const UWorld> WorldContext, bool bWasSuccessful) {
    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext.Get());
    if (SessionInt.IsValid() && FindDelegateHandle.IsValid()) {
        SessionInt->ClearOnFindSessionsCompleteDelegate_Handle(FindDelegateHandle);
        FindDelegateHandle.Reset();
    }

    CachedRooms.Empty();
    if (bWasSuccessful && ActiveSessionSearch.IsValid()) {
        CachedRooms.Reserve(ActiveSessionSearch->SearchResults.Num());
        for (const FOnlineSessionSearchResult& SearchResult : ActiveSessionSearch->SearchResults) {
            FGameNetRoomDescriptor Item;
            Item.RoomId = SearchResult.GetSessionIdStr();
            SearchResult.Session.SessionSettings.Get(SETTING_KEY_ROOM_NAME, Item.RoomName);
            SearchResult.Session.SessionSettings.Get(SETTING_KEY_MAP_PATH, Item.MapPath);
            SearchResult.Session.SessionSettings.Get(SETTING_KEY_GAME_PORT, Item.HostGamePort);
            Item.MaxPlayers = SearchResult.Session.SessionSettings.NumPublicConnections;
            Item.CurrentPlayers = FMath::Max(0, Item.MaxPlayers - SearchResult.Session.NumOpenPublicConnections);
            Item.PingMilliseconds = SearchResult.PingInMs;
            Item.bIsLanRoom = SearchResult.Session.SessionSettings.bIsLANMatch;
            CachedRooms.Add(MoveTemp(Item));
        }
    }

    OperationCompletedDelegate.Broadcast(OP_FIND_ROOMS, bWasSuccessful, FString());
}

/**
 * @brief 处理平台加入房间异步完成回调并提取连接地址串
 * @param WorldContext 世界上下文弱指针
 * @param SessionName 会话名
 * @param bSucceeded 是否成功加入
 */
void FOnlineSessionRoomAdapter::HandleJoinComplete(
    TWeakObjectPtr<const UWorld> WorldContext,
    FName SessionName,
    bool bSucceeded
) {
    if (SessionName != DEFAULT_SESSION_NAME) {
        return;
    }

    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext.Get());
    if (SessionInt.IsValid() && JoinDelegateHandle.IsValid()) {
        SessionInt->ClearOnJoinSessionCompleteDelegate_Handle(JoinDelegateHandle);
        JoinDelegateHandle.Reset();
    }

    FString ConnectString;
    if (bSucceeded && SessionInt.IsValid()) {
        SessionInt->GetResolvedConnectString(SessionName, ConnectString);
    }

    OperationCompletedDelegate.Broadcast(OP_JOIN_ROOM, bSucceeded, ConnectString);
}

/**
 * @brief 处理平台销毁房间异步完成回调
 * @param WorldContext 世界上下文弱指针
 * @param SessionName 会话名
 * @param bWasSuccessful 是否成功
 */
void FOnlineSessionRoomAdapter::HandleDestroyComplete(
    TWeakObjectPtr<const UWorld> WorldContext,
    FName SessionName,
    bool bWasSuccessful
) {
    if (SessionName != DEFAULT_SESSION_NAME) {
        return;
    }

    const IOnlineSessionPtr SessionInt = ResolveOnlineSessionInterface(WorldContext.Get());
    if (SessionInt.IsValid() && DestroyDelegateHandle.IsValid()) {
        SessionInt->ClearOnDestroySessionCompleteDelegate_Handle(DestroyDelegateHandle);
        DestroyDelegateHandle.Reset();
    }

    OperationCompletedDelegate.Broadcast(OP_DESTROY_ROOM, bWasSuccessful, FString());
}

