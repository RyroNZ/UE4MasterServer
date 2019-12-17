/* 
Copyright (c) 2019 Ryan Post

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
*/

#include "OnlineSessionInterfacePython.h"
#include "Misc/Guid.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemPython.h"
#include "OnlineSubsystemPythonTypes.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineAsyncTaskManager.h"
#include "SocketSubsystem.h"
#include "NboSerializerPython.h"
#include <SharedPointer.h>
#include "Runtime/Sockets/Public/IPAddress.h"
#include "OnlineSubsystemPythonConfig.h"
#include "Engine/World.h"


FOnlineSessionInfoPython::FOnlineSessionInfoPython() :
	HostAddr(NULL),
	SessionId(TEXT("INVALID"))
{
}

void FOnlineSessionInfoPython::Init(const FOnlineSubsystemPython& Subsystem)
{
	// Read the IP from the system
	bool bCanBindAll;
	HostAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalHostAddr(*GLog, bCanBindAll);

	// The below is a workaround for systems that set hostname to a distinct address from 127.0.0.1 on a loopback interface.
	// See e.g. https://www.debian.org/doc/manuals/debian-reference/ch05.en.html#_the_hostname_resolution
	// and http://serverfault.com/questions/363095/why-does-my-hostname-appear-with-the-address-127-0-1-1-rather-than-127-0-0-1-in
	// Since we bind to 0.0.0.0, we won't answer on 127.0.1.1, so we need to advertise ourselves as 127.0.0.1 for any other loopback address we may have.
	uint32 HostIp = 0;
	HostAddr->GetIp(HostIp); // will return in host order
	// if this address is on loopback interface, advertise it as 127.0.0.1
	if ((HostIp & 0xff000000) == 0x7f000000)
	{
		HostAddr->SetIp(0x7f000001);	// 127.0.0.1
	}

	// Now set the port that was configured
	HostAddr->SetPort(GetPortFromNetDriver(Subsystem.GetInstanceName()));

	FGuid OwnerGuid;
	FPlatformMisc::CreateGuid(OwnerGuid);
	SessionId = FUniqueNetIdPython(OwnerGuid.ToString());
}

/**
 *	Async task for ending a Python online session
 */
class FOnlineAsyncTaskNullEndSession : public FOnlineAsyncTaskBasic<FOnlineSubsystemPython>
{
private:
	/** Name of session ending */
	FName SessionName;

public:
	FOnlineAsyncTaskNullEndSession(class FOnlineSubsystemPython* InSubsystem, FName InSessionName) :
		FOnlineAsyncTaskBasic(InSubsystem),
		SessionName(InSessionName)
	{
	}

	~FOnlineAsyncTaskNullEndSession()
	{
	}

	/**
	 *	Get a human readable description of task
	 */
	virtual FString ToString() const override
	{
		return FString::Printf(TEXT("FOnlineAsyncTaskNullEndSession bWasSuccessful: %d SessionName: %s"), WasSuccessful(), *SessionName.ToString());
	}

	/**
	 * Give the async task time to do its work
	 * Can only be called on the async task manager thread
	 */
	virtual void Tick() override
	{
		bIsComplete = true;
		bWasSuccessful = true;
	}

	/**
	 * Give the async task a chance to marshal its data back to the game thread
	 * Can only be called on the game thread by the async task manager
	 */
	virtual void Finalize() override
	{
		IOnlineSessionPtr SessionInt = Subsystem->GetSessionInterface();
		FNamedOnlineSession* Session = SessionInt->GetNamedSession(SessionName);
		if (Session)
		{
			Session->SessionState = EOnlineSessionState::Ended;
		}
	}

	/**
	 *	Async task is given a chance to trigger it's delegates
	 */
	virtual void TriggerDelegates() override
	{
		IOnlineSessionPtr SessionInt = Subsystem->GetSessionInterface();
		if (SessionInt.IsValid())
		{
			SessionInt->TriggerOnEndSessionCompleteDelegates(SessionName, bWasSuccessful);
		}
	}
};

/**
 *	Async task for destroying a Python online session
 */
class FOnlineAsyncTaskNullDestroySession : public FOnlineAsyncTaskBasic<FOnlineSubsystemPython>
{
private:
	/** Name of session ending */
	FName SessionName;

public:
	FOnlineAsyncTaskNullDestroySession(class FOnlineSubsystemPython* InSubsystem, FName InSessionName) :
		FOnlineAsyncTaskBasic(InSubsystem),
		SessionName(InSessionName)
	{
	}

	~FOnlineAsyncTaskNullDestroySession()
	{
	}

	/**
	 *	Get a human readable description of task
	 */
	virtual FString ToString() const override
	{
		return FString::Printf(TEXT("FOnlineAsyncTaskNullDestroySession bWasSuccessful: %d SessionName: %s"), WasSuccessful(), *SessionName.ToString());
	}

	/**
	 * Give the async task time to do its work
	 * Can only be called on the async task manager thread
	 */
	virtual void Tick() override
	{
		bIsComplete = true;
		bWasSuccessful = true;
	}

	/**
	 * Give the async task a chance to marshal its data back to the game thread
	 * Can only be called on the game thread by the async task manager
	 */
	virtual void Finalize() override
	{
		IOnlineSessionPtr SessionInt = Subsystem->GetSessionInterface();
		if (SessionInt.IsValid())
		{
			FNamedOnlineSession* Session = SessionInt->GetNamedSession(SessionName);
			if (Session)
			{
				SessionInt->RemoveNamedSession(SessionName);
			}
		}
	}

	/**
	 *	Async task is given a chance to trigger it's delegates
	 */
	virtual void TriggerDelegates() override
	{
		IOnlineSessionPtr SessionInt = Subsystem->GetSessionInterface();
		if (SessionInt.IsValid())
		{
			SessionInt->TriggerOnDestroySessionCompleteDelegates(SessionName, bWasSuccessful);
		}
	}
};

bool FOnlineSessionPython::CreateSession(int32 HostingPlayerNum, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	uint32 Result = ONLINE_FAIL;

	// Check for an existing session
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session == NULL)
	{
		// Create a new session and deep copy the game settings
		Session = AddNamedSession(SessionName, NewSessionSettings);
		check(Session);
		CreateSessionName = SessionName;
		Session->SessionState = EOnlineSessionState::Creating;
		Session->NumOpenPrivateConnections = NewSessionSettings.NumPrivateConnections;
		Session->NumOpenPublicConnections = NewSessionSettings.NumPublicConnections;	// always start with full public connections, local player will register later

		Session->HostingPlayerNum = HostingPlayerNum;

		check(PythonSubsystem);
		IOnlineIdentityPtr Identity = PythonSubsystem->GetIdentityInterface();
		if (Identity.IsValid())
		{
			Session->OwningUserId = Identity->GetUniquePlayerId(HostingPlayerNum);
			Session->OwningUserName = Identity->GetPlayerNickname(HostingPlayerNum);
		}

		// if did not get a valid one, use just something
		if (!Session->OwningUserId.IsValid())
		{
			Session->OwningUserId = MakeShareable(new FUniqueNetIdPython(FString::Printf(TEXT("%d"), HostingPlayerNum)));
			Session->OwningUserName = FString(TEXT("NullUser"));
		}
		
		// Unique identifier of this build for compatibility
		Session->SessionSettings.BuildUniqueId = GetBuildUniqueId();

		// Setup the host session info
		FOnlineSessionInfoPython* NewSessionInfo = new FOnlineSessionInfoPython();
		NewSessionInfo->Init(*PythonSubsystem);
		Session->SessionInfo = MakeShareable(NewSessionInfo);

		if (Session->SessionSettings.bIsLANMatch)
		{

			Result = UpdateLANStatus();

			if (Result != ONLINE_IO_PENDING)
			{
				// Set the game state as pending (not started)
				Session->SessionState = EOnlineSessionState::Pending;

				if (Result != ONLINE_SUCCESS)
				{
					// Clean up the session info so we don't get into a confused state
					RemoveNamedSession(SessionName);
				}
				else
				{
					RegisterLocalPlayers(Session);
				}
			}
		}
		else
		{
			Result = ONLINE_IO_PENDING;
			FHttpModule* Http = &FHttpModule::Get();
			TSharedRef<IHttpRequest> Request = Http->CreateRequest();
			Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));
			Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			//Request->SetHeader(TEXT("Authorization"), "Basic " + APIKey);
			Request->SetVerb("GET");
			FString ServerName, MapName, GameMode;
			Session->SessionSettings.Get("SERVERNAME", ServerName);
			Session->SessionSettings.Get("MAPNAME", MapName);
			Session->SessionSettings.Get("GAMEMODE", GameMode);
			bool bPasswordProtected;
			Session->SessionSettings.Get("PASSWORDPROTECTED", bPasswordProtected);
			FOnlineSessionInfoPython* SessionInfo = (FOnlineSessionInfoPython*)Session->SessionInfo.Get();
			UOnlineSubsystemPythonConfig* config = GetMutableDefault<UOnlineSubsystemPythonConfig>();
			SetPortFromNetDriver(*PythonSubsystem, Session->SessionInfo);
			Request->SetURL(FString::Printf(TEXT("http://%s/register_server?name=%s&port=%d&maxplayers=%d&pwprotected=%s&gamemode=%s&map=%s"), *config->ServerAddress, *FGenericPlatformHttp::UrlEncode(ServerName), SessionInfo->HostAddr->GetPort(), Session->NumOpenPublicConnections, *FGenericPlatformHttp::UrlEncode(bPasswordProtected ? "true" : "false"), *FGenericPlatformHttp::UrlEncode(GameMode), *FGenericPlatformHttp::UrlEncode(MapName)));
			Request->OnProcessRequestComplete().BindRaw(this, &FOnlineSessionPython::CreateSession_ResponseReceived);
			Request->ProcessRequest();
		}
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Cannot create session '%s': session already exists."), *SessionName.ToString());
	}

	if (Result != ONLINE_IO_PENDING)
	{
		TriggerOnCreateSessionCompleteDelegates(SessionName, (Result == ONLINE_SUCCESS) ? true : false);
	}
	
	return Result == ONLINE_IO_PENDING || Result == ONLINE_SUCCESS;
}

bool FOnlineSessionPython::CreateSession(const FUniqueNetId& HostingPlayerId, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	// todo: use proper	HostingPlayerId
	return CreateSession(0, SessionName, NewSessionSettings);
}

void FOnlineSessionPython::CreateSession_ResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!Response.IsValid())
	{

		TriggerOnCreateSessionCompleteDelegates(CreateSessionName, false);
		UE_LOG_ONLINE_SESSION(Error, TEXT("Error Creating Python Session! No Response from Master Server"));
		DestroySession(CreateSessionName);
		return;
	}
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		//Get the value of the json object by field name
		bool bError = JsonObject->GetBoolField("error");
		if (!bError)
		{
			FNamedOnlineSession* Session = GetNamedSession(CreateSessionName);
			if (Session)
			{
				Session->SessionState = EOnlineSessionState::Pending;
			}
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Created Python Session!"));
			TriggerOnCreateSessionCompleteDelegates(CreateSessionName, true);
			StartHeartbeat();
		}
		else
		{
			FString ErrorMessage = JsonObject->GetStringField("message");
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Error Creating Python Session! %s"),  *ErrorMessage);
			TriggerOnCreateSessionCompleteDelegates(CreateSessionName, false);
			DestroySession(CreateSessionName);
		}
	}
}

bool FOnlineSessionPython::NeedsToAdvertise()
{
	FScopeLock ScopeLock(&SessionLock);

	bool bResult = false;
	for (int32 SessionIdx=0; SessionIdx < Sessions.Num(); SessionIdx++)
	{
		FNamedOnlineSession& Session = Sessions[SessionIdx];
		if (NeedsToAdvertise(Session))
		{
			bResult = true;
			break;
		}
	}

	return bResult;
}

bool FOnlineSessionPython::NeedsToAdvertise( FNamedOnlineSession& Session )
{
	// In Null, we have to imitate missing online service functionality, so we advertise:
	// a) LAN match with open public connections (same as usually)
	// b) Not started public LAN session (same as usually)
	// d) Joinable presence-enabled session that would be advertised with in an online service
	// (all of that only if we're server)
	return Session.SessionSettings.bShouldAdvertise && IsHost(Session) &&
		(
			(
			  Session.SessionSettings.bIsLANMatch && 			  
			  (Session.SessionState != EOnlineSessionState::InProgress || (Session.SessionSettings.bAllowJoinInProgress && Session.NumOpenPublicConnections > 0))
			) 
			||
			(
				Session.SessionSettings.bAllowJoinViaPresence || Session.SessionSettings.bAllowJoinViaPresenceFriendsOnly
			)
		);		
}

bool FOnlineSessionPython::IsSessionJoinable(const FNamedOnlineSession& Session) const
{
	const FOnlineSessionSettings& Settings = Session.SessionSettings;

	// LAN beacons are implicitly advertised.
	const bool bIsAdvertised = Settings.bShouldAdvertise || Settings.bIsLANMatch;
	const bool bIsMatchInProgress = Session.SessionState == EOnlineSessionState::InProgress;

	const bool bJoinableFromProgress = (!bIsMatchInProgress || Settings.bAllowJoinInProgress);

	const bool bAreSpacesAvailable = Session.NumOpenPublicConnections > 0;

	// LAN matches don't care about private connections / invites.
	// LAN matches don't care about presence information.
	return bIsAdvertised && bJoinableFromProgress && bAreSpacesAvailable;
}


uint32 FOnlineSessionPython::UpdateLANStatus()
{
	uint32 Result = ONLINE_SUCCESS;

	if ( NeedsToAdvertise() )
	{
		// set up LAN session
		if (LANSessionManager.GetBeaconState() == ELanBeaconState::NotUsingLanBeacon)
		{
			FOnValidQueryPacketDelegate QueryPacketDelegate = FOnValidQueryPacketDelegate::CreateRaw(this, &FOnlineSessionPython::OnValidQueryPacketReceived);
			if (!LANSessionManager.Host(QueryPacketDelegate))
			{
				Result = ONLINE_FAIL;

				LANSessionManager.StopLANSession();
			}
		}
	}
	else
	{
		if (LANSessionManager.GetBeaconState() != ELanBeaconState::Searching)
		{
			// Tear down the LAN beacon
			LANSessionManager.StopLANSession();
		}
	}

	return Result;
}

bool FOnlineSessionPython::StartSession(FName SessionName)
{
	uint32 Result = ONLINE_FAIL;
	// Grab the session information by name
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		// Can't start a match multiple times
		if (Session->SessionState == EOnlineSessionState::Pending ||
			Session->SessionState == EOnlineSessionState::Ended)
		{
			// If this lan match has join in progress disabled, shut down the beacon
			Result = UpdateLANStatus();
			Session->SessionState = EOnlineSessionState::InProgress;
		}
		else
		{
			UE_LOG_ONLINE_SESSION(Warning,	TEXT("Can't start an online session (%s) in state %s"),
				*SessionName.ToString(),
				EOnlineSessionState::ToString(Session->SessionState));
		}
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Can't start an online game for session (%s) that hasn't been created"), *SessionName.ToString());
	}

	if (Result != ONLINE_IO_PENDING)
	{
		// Just trigger the delegate
		TriggerOnStartSessionCompleteDelegates(SessionName, (Result == ONLINE_SUCCESS) ? true : false);
	}

	return Result == ONLINE_SUCCESS || Result == ONLINE_IO_PENDING;
}

bool FOnlineSessionPython::UpdateSession(FName SessionName, FOnlineSessionSettings& UpdatedSessionSettings, bool bShouldRefreshOnlineData)
{
	bool bWasSuccessful = true;
	// Grab the session information by name
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		// @TODO ONLINE update LAN settings
		Session->SessionSettings = UpdatedSessionSettings;
		FHttpModule* Http = &FHttpModule::Get();
		TSharedRef<IHttpRequest> Request = Http->CreateRequest();
		Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		//Request->SetHeader(TEXT("Authorization"), "Basic " + APIKey);
		Request->SetVerb("GET");
		FString ServerName, MapName, GameMode;
		Session->SessionSettings.Get("SERVERNAME", ServerName);
		Session->SessionSettings.Get("MAPNAME", MapName);
		Session->SessionSettings.Get("GAMEMODE", GameMode);
		bool bPasswordProtected;
		Session->SessionSettings.Get("PASSWORDPROTECTED", bPasswordProtected);

		FOnlineSessionInfoPython* SessionInfo = (FOnlineSessionInfoPython*)Session->SessionInfo.Get();
		int32 PlayerCount;
		Session->SessionSettings.Get("PLAYERCOUNT", PlayerCount);
		int32 MaxPlayers = Session->SessionSettings.NumPublicConnections;


		UOnlineSubsystemPythonConfig* config = GetMutableDefault<UOnlineSubsystemPythonConfig>();

		Request->SetURL(FString::Printf(TEXT("http://%s/update_server?name=%s&port=%d&maxplayers=%d&pwprotected=%s&gamemode=%s&map=%s&playercount=%d"), *config->ServerAddress, *FGenericPlatformHttp::UrlEncode(ServerName), SessionInfo->HostAddr->GetPort(), Session->NumOpenPublicConnections, *FGenericPlatformHttp::UrlEncode(bPasswordProtected ? "true" : "false"), *FGenericPlatformHttp::UrlEncode(GameMode), *FGenericPlatformHttp::UrlEncode(MapName), PlayerCount));
		Request->OnProcessRequestComplete().BindRaw(this, &FOnlineSessionPython::UpdateSession_ResponseReceived);
		Request->ProcessRequest();
		
	}

	return bWasSuccessful;
}

void FOnlineSessionPython::UpdateSession_ResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!Response.IsValid())
	{

		TriggerOnUpdateSessionCompleteDelegates(CreateSessionName, false);
		UE_LOG_ONLINE_SESSION(Error, TEXT("Error Creating Python Session! No Response from Master Server"));
		return;
	}
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		//Get the value of the json object by field name
		bool bError = JsonObject->GetBoolField("error");
		if (!bError)
		{
			TriggerOnUpdateSessionCompleteDelegates(CreateSessionName, true);
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Updated Python Session!"));
			StartHeartbeat();
		}
		else
		{
			TriggerOnUpdateSessionCompleteDelegates(CreateSessionName, false);
			FString ErrorMessage = JsonObject->GetStringField("message");
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Error Updating Python Session! %s"), *ErrorMessage);
		}
	}
}

bool FOnlineSessionPython::EndSession(FName SessionName)
{
	uint32 Result = ONLINE_FAIL;

	// Grab the session information by name
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		// Can't end a match that isn't in progress
		if (Session->SessionState == EOnlineSessionState::InProgress)
		{
			Session->SessionState = EOnlineSessionState::Ended;

			// If the session should be advertised and the lan beacon was destroyed, recreate
			Result = UpdateLANStatus();
		}
		else
		{
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Can't end session (%s) in state %s"),
				*SessionName.ToString(),
				EOnlineSessionState::ToString(Session->SessionState));
		}
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Can't end an online game for session (%s) that hasn't been created"),
			*SessionName.ToString());
	}

	if (Result != ONLINE_IO_PENDING)
	{
		if (Session)
		{
			Session->SessionState = EOnlineSessionState::Ended;
		}

		TriggerOnEndSessionCompleteDelegates(SessionName, (Result == ONLINE_SUCCESS) ? true : false);
	}

	return Result == ONLINE_SUCCESS || Result == ONLINE_IO_PENDING;
}

bool FOnlineSessionPython::DestroySession(FName SessionName, const FOnDestroySessionCompleteDelegate& CompletionDelegate)
{
	uint32 Result = ONLINE_FAIL;
	// Find the session in question
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		if (IsRunningDedicatedServer())
		{
			FHttpModule* Http = &FHttpModule::Get();
			TSharedRef<IHttpRequest> Request = Http->CreateRequest();
			Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));
			Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			//Request->SetHeader(TEXT("Authorization"), "Basic " + APIKey);
			Request->SetVerb("GET");
			FOnlineSessionInfoPython* SessionInfo = (FOnlineSessionInfoPython*)Session->SessionInfo.Get();
			UOnlineSubsystemPythonConfig* config = GetMutableDefault<UOnlineSubsystemPythonConfig>();
			Request->SetURL(FString::Printf(TEXT("http://%s/unregister_server?port=%d"), *config->ServerAddress, SessionInfo->HostAddr->GetPort()));
			Request->OnProcessRequestComplete().BindRaw(this, &FOnlineSessionPython::DestroySession_ResponseReceived);
			Request->ProcessRequest();
			Result = ONLINE_IO_PENDING;
		}
		else
		{
			// The session info is no longer needed
			RemoveNamedSession(Session->SessionName);

			Result = UpdateLANStatus();
		}
		
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Can't destroy a Python online session (%s)"), *SessionName.ToString());
	}

	
	if (Result != ONLINE_IO_PENDING)
	{
		CompletionDelegate.ExecuteIfBound(SessionName, (Result == ONLINE_SUCCESS) ? true : false);
		TriggerOnDestroySessionCompleteDelegates(SessionName, (Result == ONLINE_SUCCESS) ? true : false);
	}

	return Result == ONLINE_SUCCESS || Result == ONLINE_IO_PENDING;
}

void FOnlineSessionPython::DestroySession_ResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!Response.IsValid())
	{

		TriggerOnDestroySessionCompleteDelegates(CreateSessionName, false);
		UE_LOG_ONLINE_SESSION(Error, TEXT("Error destroying Python Session! No Response from Master Server"));
		return;
	}
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		//Get the value of the json object by field name
		bool bError = JsonObject->GetBoolField("error");
		if (!bError)
		{
			TriggerOnDestroySessionCompleteDelegates(CreateSessionName, true);
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Destroyed Python Session!"));
		}
		else
		{
			TriggerOnDestroySessionCompleteDelegates(CreateSessionName, false);
			FString ErrorMessage = JsonObject->GetStringField("message");
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Error destroying Python Session! %s"), *ErrorMessage);
		}
	}
}

bool FOnlineSessionPython::IsPlayerInSession(FName SessionName, const FUniqueNetId& UniqueId)
{
	return IsPlayerInSessionImpl(this, SessionName, UniqueId);
}

bool FOnlineSessionPython::StartMatchmaking(const TArray< TSharedRef<const FUniqueNetId> >& LocalPlayers, FName SessionName, const FOnlineSessionSettings& NewSessionSettings, TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("StartMatchmaking is not supported on this platform. Use FindSessions or FindSessionById."));
	TriggerOnMatchmakingCompleteDelegates(SessionName, false);
	return false;
}

bool FOnlineSessionPython::CancelMatchmaking(int32 SearchingPlayerNum, FName SessionName)
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("CancelMatchmaking is not supported on this platform. Use CancelFindSessions."));
	TriggerOnCancelMatchmakingCompleteDelegates(SessionName, false);
	return false;
}

bool FOnlineSessionPython::CancelMatchmaking(const FUniqueNetId& SearchingPlayerId, FName SessionName)
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("CancelMatchmaking is not supported on this platform. Use CancelFindSessions."));
	TriggerOnCancelMatchmakingCompleteDelegates(SessionName, false);
	return false;
}

bool FOnlineSessionPython::FindSessions(int32 SearchingPlayerNum, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	uint32 Return = ONLINE_FAIL;

	// Don't start another search while one is in progress
	if (!CurrentSessionSearch.IsValid() && SearchSettings->SearchState != EOnlineAsyncTaskState::InProgress)
	{
		// Free up previous results
		SearchSettings->SearchResults.Empty();

		// Copy the search pointer so we can keep it around
		CurrentSessionSearch = SearchSettings;

		// remember the time at which we started search, as this will be used for a "good enough" ping estimation
		SessionSearchStartInSeconds = FPlatformTime::Seconds();

		if (SearchSettings->bIsLanQuery)
		{
			// Check if its a LAN query
			Return = FindLANSession();

			if (Return == ONLINE_IO_PENDING)
			{
				SearchSettings->SearchState = EOnlineAsyncTaskState::InProgress;
			}
		}
		else
		{
			FHttpModule* Http = &FHttpModule::Get();
			TSharedRef<IHttpRequest> Request = Http->CreateRequest();
			Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));
			Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			//Request->SetHeader(TEXT("Authorization"), "Basic " + APIKey);
			Request->SetVerb("GET");
			UOnlineSubsystemPythonConfig* config = GetMutableDefault<UOnlineSubsystemPythonConfig>();
			Request->SetURL(FString::Printf(TEXT("http://%s/get_serverlist"), *config->ServerAddress));
			Request->OnProcessRequestComplete().BindRaw(this, &FOnlineSessionPython::FindSessions_ResponseReceived);
			Request->ProcessRequest();
			Return = ONLINE_IO_PENDING;
		}
		
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Ignoring game search request while one is pending"));
		Return = ONLINE_IO_PENDING;
	}

	return Return == ONLINE_SUCCESS || Return == ONLINE_IO_PENDING;
}

bool FOnlineSessionPython::FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	// This function doesn't use the SearchingPlayerNum parameter, so passing in anything is fine.
	return FindSessions(0, SearchSettings);
}

bool FOnlineSessionPython::FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegates)
{
	FOnlineSessionSearchResult EmptyResult;
	CompletionDelegates.ExecuteIfBound(0, false, EmptyResult);
	return true;
}

void FOnlineSessionPython::FindSessions_ResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!Response.IsValid())
	{

		TriggerOnFindSessionsCompleteDelegates(true);
		UE_LOG_ONLINE_SESSION(Error, TEXT("Error finding Python Sessions! No Response from Master Server"));
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		//Get the value of the json object by field name
		bool bError = JsonObject->GetBoolField("error");
		if (!bError)
		{
			
			TArray <TSharedPtr<FJsonValue>> JsonServerList = JsonObject->GetArrayField("servers");
			for (int32 i = 0; i != JsonServerList.Num(); i++)
			{
				FOnlineSessionSearchResult SearchResult;
				TSharedPtr<FOnlineSessionInfoPython> SessionInfo = MakeShareable(new FOnlineSessionInfoPython());;
				auto server = JsonServerList[i]->AsObject();
				TSharedPtr<FInternetAddr> InternetAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
				InternetAddress->SetPort(server->GetIntegerField("port"));
				bool bIsValid;
				InternetAddress->SetIp(*server->GetStringField("ip"), bIsValid);
				SessionInfo->HostAddr = InternetAddress;
				SearchResult.Session.SessionInfo = SessionInfo;
				SearchResult.Session.NumOpenPublicConnections = server->GetIntegerField("maxplayers");
				SearchResult.Session.SessionSettings.Set(SETTING_MAPNAME, *server->GetStringField("mapname"));
				SearchResult.Session.SessionSettings.Set(SETTING_GAMEMODE, *server->GetStringField("gamemode"));
				SearchResult.Session.SessionSettings.Set("SERVERNAME", *server->GetStringField("name"));
				SearchResult.Session.SessionSettings.Set(FName(TEXT("PASSWORDPROTECTED")), server->GetStringField("pwprotected"));
				SearchResult.Session.SessionSettings.Set("PLAYERCOUNT", server->GetIntegerField("playercount"));
				SearchResult.Session.SessionSettings.Set("MAXPLAYERS", server->GetIntegerField("maxplayers"));

				CurrentSessionSearch->SearchResults.Add(SearchResult);
			}
			TriggerOnFindSessionsCompleteDelegates(true);
			CurrentSessionSearch = nullptr;
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Found Python Sessions!"));
		}
		else
		{
			TriggerOnFindSessionsCompleteDelegates(false);
			FString ErrorMessage = JsonObject->GetStringField("message");
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Error finding Python Sessions! %s"), *ErrorMessage);
			CurrentSessionSearch = nullptr;
		}
	}
	CurrentSessionSearch = nullptr;
	TriggerOnFindSessionsCompleteDelegates(false);
}

uint32 FOnlineSessionPython::FindLANSession()
{
	uint32 Return = ONLINE_IO_PENDING;

	// Recreate the unique identifier for this client
	GenerateNonce((uint8*)&LANSessionManager.LanNonce, 8);

	FOnValidResponsePacketDelegate ResponseDelegate = FOnValidResponsePacketDelegate::CreateRaw(this, &FOnlineSessionPython::OnValidResponsePacketReceived);
	FOnSearchingTimeoutDelegate TimeoutDelegate = FOnSearchingTimeoutDelegate::CreateRaw(this, &FOnlineSessionPython::OnLANSearchTimeout);

	FNboSerializeToBufferNull Packet(LAN_BEACON_MAX_PACKET_SIZE);
	LANSessionManager.CreateClientQueryPacket(Packet, LANSessionManager.LanNonce);
	if (LANSessionManager.Search(Packet, ResponseDelegate, TimeoutDelegate) == false)
	{
		Return = ONLINE_FAIL;

		FinalizeLANSearch();

		CurrentSessionSearch->SearchState = EOnlineAsyncTaskState::Failed;
		
		// Just trigger the delegate as having failed
		TriggerOnFindSessionsCompleteDelegates(false);
	}
	return Return;
}

bool FOnlineSessionPython::CancelFindSessions()
{
	uint32 Return = ONLINE_FAIL;
	if (CurrentSessionSearch.IsValid() && CurrentSessionSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		// Make sure it's the right type
		Return = ONLINE_SUCCESS;

		FinalizeLANSearch();

		CurrentSessionSearch->SearchState = EOnlineAsyncTaskState::Failed;
		CurrentSessionSearch = NULL;
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Can't cancel a search that isn't in progress"));
	}

	if (Return != ONLINE_IO_PENDING)
	{
		TriggerOnCancelFindSessionsCompleteDelegates(true);
	}

	return Return == ONLINE_SUCCESS || Return == ONLINE_IO_PENDING;
}

bool FOnlineSessionPython::JoinSession(int32 PlayerNum, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	uint32 Return = ONLINE_FAIL;
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	// Don't join a session if already in one or hosting one
	if (Session == NULL)
	{
		// Create a named session from the search result data
		Session = AddNamedSession(SessionName, DesiredSession.Session);
		Session->HostingPlayerNum = PlayerNum;

		// Create Internet or LAN match
		FOnlineSessionInfoPython* NewSessionInfo = new FOnlineSessionInfoPython();
		Session->SessionInfo = MakeShareable(NewSessionInfo);

		Return = JoinLANSession(PlayerNum, Session, &DesiredSession.Session);

		// turn off advertising on Join, to avoid clients advertising it over LAN
		Session->SessionSettings.bShouldAdvertise = false;

		if (Return != ONLINE_IO_PENDING)
		{
			if (Return != ONLINE_SUCCESS)
			{
				// Clean up the session info so we don't get into a confused state
				RemoveNamedSession(SessionName);
			}
			else
			{
				RegisterLocalPlayers(Session);
			}
		}
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Session (%s) already exists, can't join twice"), *SessionName.ToString());
	}

	if (Return != ONLINE_IO_PENDING)
	{
		// Just trigger the delegate as having failed
		TriggerOnJoinSessionCompleteDelegates(SessionName, Return == ONLINE_SUCCESS ? EOnJoinSessionCompleteResult::Success : EOnJoinSessionCompleteResult::UnknownError);
	}

	return Return == ONLINE_SUCCESS || Return == ONLINE_IO_PENDING;
}

bool FOnlineSessionPython::JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	// Assuming player 0 should be OK here
	return JoinSession(0, SessionName, DesiredSession);
}

bool FOnlineSessionPython::FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Python subsystem
	TArray<FOnlineSessionSearchResult> EmptySearchResult;
	TriggerOnFindFriendSessionCompleteDelegates(LocalUserNum, false, EmptySearchResult);
	return false;
};

bool FOnlineSessionPython::FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Python subsystem
	TArray<FOnlineSessionSearchResult> EmptySearchResult;
	TriggerOnFindFriendSessionCompleteDelegates(0, false, EmptySearchResult);
	return false;
}

bool FOnlineSessionPython::FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& FriendList)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Python subsystem
	TArray<FOnlineSessionSearchResult> EmptySearchResult;
	TriggerOnFindFriendSessionCompleteDelegates(0, false, EmptySearchResult);
	return false;
}

bool FOnlineSessionPython::SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Python subsystem
	return false;
};

bool FOnlineSessionPython::SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Python subsystem
	return false;
}

bool FOnlineSessionPython::SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Python subsystem
	return false;
};

bool FOnlineSessionPython::SendSessionInviteToFriends(const FUniqueNetId& LocalUserId, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends)
{
	// this function has to exist due to interface definition, but it does not have a meaningful implementation in Python subsystem
	return false;
}

uint32 FOnlineSessionPython::JoinLANSession(int32 PlayerNum, FNamedOnlineSession* Session, const FOnlineSession* SearchSession)
{
	check(Session != nullptr);

	uint32 Result = ONLINE_FAIL;
	Session->SessionState = EOnlineSessionState::Pending;

	if (Session->SessionInfo.IsValid() && SearchSession != nullptr && SearchSession->SessionInfo.IsValid())
	{
		// Copy the session info over
		const FOnlineSessionInfoPython* SearchSessionInfo = (const FOnlineSessionInfoPython*)SearchSession->SessionInfo.Get();
		FOnlineSessionInfoPython* SessionInfo = (FOnlineSessionInfoPython*)Session->SessionInfo.Get();
		SessionInfo->SessionId = SearchSessionInfo->SessionId;

		SessionInfo->HostAddr = SearchSessionInfo->HostAddr->Clone();
		Result = ONLINE_SUCCESS;
	}

	return Result;
}

bool FOnlineSessionPython::PingSearchResults(const FOnlineSessionSearchResult& SearchResult)
{
	return false;
}

/** Get a resolved connection string from a session info */
static bool GetConnectStringFromSessionInfo(TSharedPtr<FOnlineSessionInfoPython>& SessionInfo, FString& ConnectInfo, int32 PortOverride=0)
{
	bool bSuccess = false;
	if (SessionInfo.IsValid())
	{
		if (SessionInfo->HostAddr.IsValid() && SessionInfo->HostAddr->IsValid())
		{
			if (PortOverride != 0)
			{
				ConnectInfo = FString::Printf(TEXT("%s:%d"), *SessionInfo->HostAddr->ToString(false), PortOverride);
			}
			else
			{
				ConnectInfo = FString::Printf(TEXT("%s"), *SessionInfo->HostAddr->ToString(true));
			}

			bSuccess = true;
		}
	}

	return bSuccess;
}

bool FOnlineSessionPython::GetResolvedConnectString(FName SessionName, FString& ConnectInfo, FName PortType)
{
	bool bSuccess = false;
	// Find the session
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session != NULL)
	{
		TSharedPtr<FOnlineSessionInfoPython> SessionInfo = StaticCastSharedPtr<FOnlineSessionInfoPython>(Session->SessionInfo);
		if (PortType == NAME_BeaconPort)
		{
			int32 BeaconListenPort = GetBeaconPortFromSessionSettings(Session->SessionSettings);
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo, BeaconListenPort);
		}
		else if (PortType == NAME_GamePort)
		{
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo);
		}

		if (!bSuccess)
		{
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Invalid session info for session %s in GetResolvedConnectString()"), *SessionName.ToString());
		}
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning,
			TEXT("Unknown session name (%s) specified to GetResolvedConnectString()"),
			*SessionName.ToString());
	}

	return bSuccess;
}

bool FOnlineSessionPython::GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo)
{
	bool bSuccess = false;
	if (SearchResult.Session.SessionInfo.IsValid())
	{
		TSharedPtr<FOnlineSessionInfoPython> SessionInfo = StaticCastSharedPtr<FOnlineSessionInfoPython>(SearchResult.Session.SessionInfo);

		if (PortType == NAME_BeaconPort)
		{
			int32 BeaconListenPort = GetBeaconPortFromSessionSettings(SearchResult.Session.SessionSettings);
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo, BeaconListenPort);

		}
		else if (PortType == NAME_GamePort)
		{
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo);
		}
	}
	
	if (!bSuccess || ConnectInfo.IsEmpty())
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Invalid session info in search result to GetResolvedConnectString()"));
	}

	return bSuccess;
}

FOnlineSessionSettings* FOnlineSessionPython::GetSessionSettings(FName SessionName) 
{
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		return &Session->SessionSettings;
	}
	return NULL;
}

void FOnlineSessionPython::RegisterLocalPlayers(FNamedOnlineSession* Session)
{
	if (!PythonSubsystem->IsDedicated())
	{
		IOnlineVoicePtr VoiceInt = PythonSubsystem->GetVoiceInterface();
		if (VoiceInt.IsValid())
		{
			for (int32 Index = 0; Index < MAX_LOCAL_PLAYERS; Index++)
			{
				// Register the local player as a local talker
				VoiceInt->RegisterLocalTalker(Index);
			}
		}
	}
}

void FOnlineSessionPython::RegisterVoice(const FUniqueNetId& PlayerId)
{
	IOnlineVoicePtr VoiceInt = PythonSubsystem->GetVoiceInterface();
	if (VoiceInt.IsValid())
	{
		if (!PythonSubsystem->IsLocalPlayer(PlayerId))
		{
			VoiceInt->RegisterRemoteTalker(PlayerId);
		}
		else
		{
			// This is a local player. In case their PlayerState came last during replication, reprocess muting
			VoiceInt->ProcessMuteChangeNotification();
		}
	}
}

void FOnlineSessionPython::UnregisterVoice(const FUniqueNetId& PlayerId)
{
	IOnlineVoicePtr VoiceInt = PythonSubsystem->GetVoiceInterface();
	if (VoiceInt.IsValid())
	{
		if (!PythonSubsystem->IsLocalPlayer(PlayerId))
		{
			if (VoiceInt.IsValid())
			{
				VoiceInt->UnregisterRemoteTalker(PlayerId);
			}
		}
	}
}

bool FOnlineSessionPython::RegisterPlayer(FName SessionName, const FUniqueNetId& PlayerId, bool bWasInvited)
{
	TArray< TSharedRef<const FUniqueNetId> > Players;
	Players.Add(MakeShareable(new FUniqueNetIdPython(PlayerId)));
	return RegisterPlayers(SessionName, Players, bWasInvited);
}

bool FOnlineSessionPython::RegisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players, bool bWasInvited)
{
	bool bSuccess = false;
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		bSuccess = true;

		for (int32 PlayerIdx=0; PlayerIdx<Players.Num(); PlayerIdx++)
		{
			const TSharedRef<const FUniqueNetId>& PlayerId = Players[PlayerIdx];

			FUniqueNetIdMatcher PlayerMatch(*PlayerId);
			if (Session->RegisteredPlayers.IndexOfByPredicate(PlayerMatch) == INDEX_NONE)
			{
				Session->RegisteredPlayers.Add(PlayerId);
				RegisterVoice(*PlayerId);

				// update number of open connections
				if (Session->NumOpenPublicConnections > 0)
				{
					Session->NumOpenPublicConnections--;
				}
				else if (Session->NumOpenPrivateConnections > 0)
				{
					Session->NumOpenPrivateConnections--;
				}
			}
			else
			{
				RegisterVoice(*PlayerId);
				UE_LOG_ONLINE_SESSION(Log, TEXT("Player %s already registered in session %s"), *PlayerId->ToDebugString(), *SessionName.ToString());
			}			
		}
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("No game present to join for session (%s)"), *SessionName.ToString());
	}

	TriggerOnRegisterPlayersCompleteDelegates(SessionName, Players, bSuccess);
	return bSuccess;
}

bool FOnlineSessionPython::UnregisterPlayer(FName SessionName, const FUniqueNetId& PlayerId)
{
	TArray< TSharedRef<const FUniqueNetId> > Players;
	Players.Add(MakeShareable(new FUniqueNetIdPython(PlayerId)));
	return UnregisterPlayers(SessionName, Players);
}

bool FOnlineSessionPython::UnregisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players)
{
	bool bSuccess = true;

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		for (int32 PlayerIdx=0; PlayerIdx < Players.Num(); PlayerIdx++)
		{
			const TSharedRef<const FUniqueNetId>& PlayerId = Players[PlayerIdx];

			FUniqueNetIdMatcher PlayerMatch(*PlayerId);
			int32 RegistrantIndex = Session->RegisteredPlayers.IndexOfByPredicate(PlayerMatch);
			if (RegistrantIndex != INDEX_NONE)
			{
				Session->RegisteredPlayers.RemoveAtSwap(RegistrantIndex);
				UnregisterVoice(*PlayerId);

				// update number of open connections
				if (Session->NumOpenPublicConnections < Session->SessionSettings.NumPublicConnections)
				{
					Session->NumOpenPublicConnections++;
				}
				else if (Session->NumOpenPrivateConnections < Session->SessionSettings.NumPrivateConnections)
				{
					Session->NumOpenPrivateConnections++;
				}
			}
			else
			{
				UE_LOG_ONLINE_SESSION(Warning, TEXT("Player %s is not part of session (%s)"), *PlayerId->ToDebugString(), *SessionName.ToString());
			}
		}
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("No game present to leave for session (%s)"), *SessionName.ToString());
		bSuccess = false;
	}

	TriggerOnUnregisterPlayersCompleteDelegates(SessionName, Players, bSuccess);
	return bSuccess;
}

void FOnlineSessionPython::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_Session_Interface);
	TickLanTasks(DeltaTime);
}

void FOnlineSessionPython::TickLanTasks(float DeltaTime)
{
	LANSessionManager.Tick(DeltaTime);
}

void FOnlineSessionPython::AppendSessionToPacket(FNboSerializeToBufferNull& Packet, FOnlineSession* Session)
{
	/** Owner of the session */
	Packet << *StaticCastSharedPtr<const FUniqueNetIdPython>(Session->OwningUserId)
		<< Session->OwningUserName
		<< Session->NumOpenPrivateConnections
		<< Session->NumOpenPublicConnections;

	// Try to get the actual port the netdriver is using
	SetPortFromNetDriver(*PythonSubsystem, Session->SessionInfo);

	// Write host info (host addr, session id, and key)
	Packet << *StaticCastSharedPtr<FOnlineSessionInfoPython>(Session->SessionInfo);

	// Now append per game settings
	AppendSessionSettingsToPacket(Packet, &Session->SessionSettings);
}

void FOnlineSessionPython::AppendSessionSettingsToPacket(FNboSerializeToBufferNull& Packet, FOnlineSessionSettings* SessionSettings)
{
#if DEBUG_LAN_BEACON
	UE_LOG_ONLINE_SESSION(Verbose, TEXT("Sending session settings to client"));
#endif 

	// Members of the session settings class
	Packet << SessionSettings->NumPublicConnections
		<< SessionSettings->NumPrivateConnections
		<< (uint8)SessionSettings->bShouldAdvertise
		<< (uint8)SessionSettings->bIsLANMatch
		<< (uint8)SessionSettings->bIsDedicated
		<< (uint8)SessionSettings->bUsesStats
		<< (uint8)SessionSettings->bAllowJoinInProgress
		<< (uint8)SessionSettings->bAllowInvites
		<< (uint8)SessionSettings->bUsesPresence
		<< (uint8)SessionSettings->bAllowJoinViaPresence
		<< (uint8)SessionSettings->bAllowJoinViaPresenceFriendsOnly
		<< (uint8)SessionSettings->bAntiCheatProtected
	    << SessionSettings->BuildUniqueId;

	// First count number of advertised keys
	int32 NumAdvertisedProperties = 0;
	for (FSessionSettings::TConstIterator It(SessionSettings->Settings); It; ++It)
	{	
		const FOnlineSessionSetting& Setting = It.Value();
		if (Setting.AdvertisementType >= EOnlineDataAdvertisementType::ViaOnlineService)
		{
			NumAdvertisedProperties++;
		}
	}

	// Add count of advertised keys and the data
	Packet << (int32)NumAdvertisedProperties;
	for (FSessionSettings::TConstIterator It(SessionSettings->Settings); It; ++It)
	{
		const FOnlineSessionSetting& Setting = It.Value();
		if (Setting.AdvertisementType >= EOnlineDataAdvertisementType::ViaOnlineService)
		{
			Packet << It.Key();
			Packet << Setting;
#if DEBUG_LAN_BEACON
			UE_LOG_ONLINE_SESSION(Verbose, TEXT("%s"), *Setting.ToString());
#endif
		}
	}
}

void FOnlineSessionPython::OnValidQueryPacketReceived(uint8* PacketData, int32 PacketLength, uint64 ClientNonce)
{
	// Iterate through all registered sessions and respond for each one that can be joinable
	FScopeLock ScopeLock(&SessionLock);
	for (int32 SessionIndex = 0; SessionIndex < Sessions.Num(); SessionIndex++)
	{
		FNamedOnlineSession* Session = &Sessions[SessionIndex];

		// Don't respond to query if the session is not a joinable LAN match.
		if (Session && IsSessionJoinable(*Session))
		{
			FNboSerializeToBufferNull Packet(LAN_BEACON_MAX_PACKET_SIZE);
			// Create the basic header before appending additional information
			LANSessionManager.CreateHostResponsePacket(Packet, ClientNonce);

			// Add all the session details
			AppendSessionToPacket(Packet, Session);

			// Broadcast this response so the client can see us
			if (!Packet.HasOverflow())
			{
				LANSessionManager.BroadcastPacket(Packet, Packet.GetByteCount());
			}
			else
			{
				UE_LOG_ONLINE_SESSION(Warning, TEXT("LAN broadcast packet overflow, cannot broadcast on LAN"));
			}
		}
	}
}

void FOnlineSessionPython::ReadSessionFromPacket(FNboSerializeFromBufferNull& Packet, FOnlineSession* Session)
{
#if DEBUG_LAN_BEACON
	UE_LOG_ONLINE_SESSION(Verbose, TEXT("Reading session information from server"));
#endif

	/** Owner of the session */
	FUniqueNetIdPython* UniqueId = new FUniqueNetIdPython;
	Packet >> *UniqueId
		>> Session->OwningUserName
		>> Session->NumOpenPrivateConnections
		>> Session->NumOpenPublicConnections;

	Session->OwningUserId = MakeShareable(UniqueId);

	// Allocate and read the connection data
	FOnlineSessionInfoPython* NullSessionInfo = new FOnlineSessionInfoPython();
	NullSessionInfo->HostAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	Packet >> *NullSessionInfo;
	Session->SessionInfo = MakeShareable(NullSessionInfo); 

	// Read any per object data using the server object
	ReadSettingsFromPacket(Packet, Session->SessionSettings);
}

void FOnlineSessionPython::ReadSettingsFromPacket(FNboSerializeFromBufferNull& Packet, FOnlineSessionSettings& SessionSettings)
{
#if DEBUG_LAN_BEACON
	UE_LOG_ONLINE_SESSION(Verbose, TEXT("Reading game settings from server"));
#endif

	// Clear out any old settings
	SessionSettings.Settings.Empty();

	// Members of the session settings class
	Packet >> SessionSettings.NumPublicConnections
		>> SessionSettings.NumPrivateConnections;
	uint8 Read = 0;
	// Read all the bools as bytes
	Packet >> Read;
	SessionSettings.bShouldAdvertise = !!Read;
	Packet >> Read;
	SessionSettings.bIsLANMatch = !!Read;
	Packet >> Read;
	SessionSettings.bIsDedicated = !!Read;
	Packet >> Read;
	SessionSettings.bUsesStats = !!Read;
	Packet >> Read;
	SessionSettings.bAllowJoinInProgress = !!Read;
	Packet >> Read;
	SessionSettings.bAllowInvites = !!Read;
	Packet >> Read;
	SessionSettings.bUsesPresence = !!Read;
	Packet >> Read;
	SessionSettings.bAllowJoinViaPresence = !!Read;
	Packet >> Read;
	SessionSettings.bAllowJoinViaPresenceFriendsOnly = !!Read;
	Packet >> Read;
	SessionSettings.bAntiCheatProtected = !!Read;

	// BuildId
	Packet >> SessionSettings.BuildUniqueId;

	// Now read the contexts and properties from the settings class
	int32 NumAdvertisedProperties = 0;
	// First, read the number of advertised properties involved, so we can presize the array
	Packet >> NumAdvertisedProperties;
	if (Packet.HasOverflow() == false)
	{
		FName Key;
		// Now read each context individually
		for (int32 Index = 0;
			Index < NumAdvertisedProperties && Packet.HasOverflow() == false;
			Index++)
		{
			FOnlineSessionSetting Setting;
			Packet >> Key;
			Packet >> Setting;
			SessionSettings.Set(Key, Setting);

#if DEBUG_LAN_BEACON
			UE_LOG_ONLINE_SESSION(Verbose, TEXT("%s"), *Setting->ToString());
#endif
		}
	}
	
	// If there was an overflow, treat the string settings/properties as broken
	if (Packet.HasOverflow())
	{
		SessionSettings.Settings.Empty();
		UE_LOG_ONLINE_SESSION(Verbose, TEXT("Packet overflow detected in ReadGameSettingsFromPacket()"));
	}
}

void FOnlineSessionPython::OnValidResponsePacketReceived(uint8* PacketData, int32 PacketLength)
{
	// Create an object that we'll copy the data to
	FOnlineSessionSettings NewServer;
	if (CurrentSessionSearch.IsValid())
	{
		// Add space in the search results array
		FOnlineSessionSearchResult* NewResult = new (CurrentSessionSearch->SearchResults) FOnlineSessionSearchResult();
		// this is not a correct ping, but better than nothing
		NewResult->PingInMs = static_cast<int32>((FPlatformTime::Seconds() - SessionSearchStartInSeconds) * 1000);

		FOnlineSession* NewSession = &NewResult->Session;

		// Prepare to read data from the packet
		FNboSerializeFromBufferNull Packet(PacketData, PacketLength);
		
		ReadSessionFromPacket(Packet, NewSession);

		// NOTE: we don't notify until the timeout happens
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Failed to create new online game settings object"));
	}
}

uint32 FOnlineSessionPython::FinalizeLANSearch()
{
	if (LANSessionManager.GetBeaconState() == ELanBeaconState::Searching)
	{
		LANSessionManager.StopLANSession();
	}

	return UpdateLANStatus();
}

void FOnlineSessionPython::OnLANSearchTimeout()
{
	FinalizeLANSearch();

	if (CurrentSessionSearch.IsValid())
	{
		if (CurrentSessionSearch->SearchResults.Num() > 0)
		{
			// Allow game code to sort the servers
			CurrentSessionSearch->SortSearchResults();
		}
		CurrentSessionSearch->SearchState = EOnlineAsyncTaskState::Done;

		CurrentSessionSearch = NULL;
	}

	// Trigger the delegate as complete
	TriggerOnFindSessionsCompleteDelegates(true);
}

int32 FOnlineSessionPython::GetNumSessions()
{
	FScopeLock ScopeLock(&SessionLock);
	return Sessions.Num();
}

void FOnlineSessionPython::DumpSessionState()
{
	FScopeLock ScopeLock(&SessionLock);

	for (int32 SessionIdx=0; SessionIdx < Sessions.Num(); SessionIdx++)
	{
		DumpNamedSession(&Sessions[SessionIdx]);
	}
}

void FOnlineSessionPython::StartHeartbeat()
{
#if WITH_ENGINE
	if (GEngine)
	{
		const FOnlineSubsystemPython& Subsystem = *PythonSubsystem;
		UWorld* World = GetWorldForOnline(Subsystem.GetInstanceName());

		World->GetTimerManager().SetTimer(PerformHeartbeat_Handle, FTimerDelegate::CreateRaw(this, &FOnlineSessionPython::PerformHeartbeat), 50.0f, true, 1.0f);
	}
#endif
}

void FOnlineSessionPython::PerformHeartbeat()
{
	FHttpModule* Http = &FHttpModule::Get();
	TSharedRef<IHttpRequest> Request = Http->CreateRequest();
	Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetVerb("GET");
	FNamedOnlineSession* Session = GetNamedSession(CreateSessionName);
	FOnlineSessionInfoPython* SessionInfo = (FOnlineSessionInfoPython*)Session->SessionInfo.Get();
	SetPortFromNetDriver(*PythonSubsystem, Session->SessionInfo);
	UOnlineSubsystemPythonConfig* config = GetMutableDefault<UOnlineSubsystemPythonConfig>();
	
	Request->SetURL(FString::Printf(TEXT("http://%s/perform_heartbeat?port=%d"), *config->ServerAddress, SessionInfo->HostAddr->GetPort()));
	Request->ProcessRequest();
}

void FOnlineSessionPython::RegisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnRegisterLocalPlayerCompleteDelegate& Delegate)
{
	Delegate.ExecuteIfBound(PlayerId, EOnJoinSessionCompleteResult::Success);
}

void FOnlineSessionPython::UnregisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnUnregisterLocalPlayerCompleteDelegate& Delegate)
{
	Delegate.ExecuteIfBound(PlayerId, true);
}

void FOnlineSessionPython::SetPortFromNetDriver(const FOnlineSubsystemPython& Subsystem, const TSharedPtr<FOnlineSessionInfo>& SessionInfo)
{
	auto NetDriverPort = 0;
#if WITH_ENGINE
	if (GEngine)
	{
		UWorld* World = GetWorldForOnline(Subsystem.GetInstanceName());
		FString LString, RString;
		World->GetAddressURL().Split(":", &LString, &RString, ESearchCase::IgnoreCase);
		NetDriverPort = FCString::Atoi(*RString);
	}
#endif
	auto SessionInfoPython = StaticCastSharedPtr<FOnlineSessionInfoPython>(SessionInfo);
	if (SessionInfoPython.IsValid() && SessionInfoPython->HostAddr.IsValid())
	{
		SessionInfoPython->HostAddr->SetPort(NetDriverPort);
	}
}

bool FOnlineSessionPython::IsHost(const FNamedOnlineSession& Session) const
{
	if (PythonSubsystem->IsDedicated())
	{
		return true;
	}

	IOnlineIdentityPtr IdentityInt = PythonSubsystem->GetIdentityInterface(); 
	if (!IdentityInt.IsValid())
	{
		return false;
	}

	TSharedPtr<const FUniqueNetId> UserId = IdentityInt->GetUniquePlayerId(Session.HostingPlayerNum);
	return (UserId.IsValid() && (*UserId == *Session.OwningUserId));
}

TSharedPtr<const FUniqueNetId> FOnlineSessionPython::CreateSessionIdFromString(const FString& SessionIdStr)
{
	TSharedPtr<const FUniqueNetId> SessionId;
	if (!SessionIdStr.IsEmpty())
	{
		SessionId = MakeShared<FUniqueNetIdPython>(SessionIdStr);
	}
	return SessionId;
}
