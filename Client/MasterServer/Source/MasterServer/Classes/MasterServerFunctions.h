/*

			RyroNZ's Networking Library for UE4

Networking library designed to interface with RyroNZ's Master Server.
Copyright(C) 2015  Ryan Post

This program is free software : you can redistribute it and / or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.If not, see < http://www.gnu.org/licenses/>.

*/


UENUM(BlueprintType)
enum class EHttpRequestType : uint8
{
	HRT_None		            UMETA(DisplayName = "None"),
	HRT_RegisterServer          UMETA(DisplayName = "Register Server"),
	HRT_UnregisterServer		UMETA(DisplayName = "Unregister Server"),
	HRT_CheckIn					UMETA(DisplayName = "Check In"),
	HRT_ReceiveServerList		UMETA(DisplayName = "Receive Server List"),
	HRT_PingServer				UMETA(DisplayName = "Ping Server")
};

UENUM(BlueprintType)
enum class EHttpResponse : uint8
{
	HR_None			UMETA(DisplayName = "Unknown Error!"),
	HR_Success		UMETA(DisplayName = "Success!"),
	HR_NoData			UMETA(DisplayName = "Received no data!"),
	HR_ParseError		UMETA(DisplayName = "Unable to parse response!"),
	HR_NoSend			UMETA(DisplayName = "Failed to send request!")

};


#include "Http.h"
#include "httpd.h"
#include <ctime>

// Bug requires .generated to be included after enums for delegates using enum types.
#include "MasterServerFunctions.generated.h"

USTRUCT(BlueprintType)
struct FServerInformation
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Information")
		FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Information")
		FString Ip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Information")
		FString Port;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Information")
		FString GameMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Information")
		FString Map;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Information")
		int32 MaxPlayers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Information")
		int32 CurrentPlayers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Information")
		int32 Ping;


	FString GetFullAddress()
	{
		return Ip + ":" + Port;
	}

	void GetDataFromJSON(TSharedPtr<FJsonObject> JsonArray)
	{
		Name = JsonArray->GetStringField("name");
		Ip = JsonArray->GetStringField("ip");
		Port = JsonArray->GetStringField("port");
		GameMode = JsonArray->GetStringField("game_mode");
		Map = JsonArray->GetStringField("map");
		MaxPlayers = JsonArray->GetNumberField("max_players");
		CurrentPlayers = JsonArray->GetNumberField("current_players");
	}

	TSharedPtr<FJsonObject> GenerateJSONObject()
	{
		TSharedPtr<FJsonObject> OutJsonArray = MakeShareable(new FJsonObject());

		OutJsonArray->SetStringField("name", Name);
		OutJsonArray->SetStringField("port", Port);
		OutJsonArray->SetStringField("game_mode", GameMode);
		OutJsonArray->SetStringField("map", Map);
		OutJsonArray->SetNumberField("max_players", MaxPlayers);
		OutJsonArray->SetNumberField("current_players", CurrentPlayers);

		return OutJsonArray;
	}

	bool IsEmpty()
	{
		if (Name.IsEmpty() || Port.IsEmpty())
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	FServerInformation()
	{
		Name = ""; Ip = ""; Port = ""; GameMode = ""; Map = ""; MaxPlayers = -1; CurrentPlayers = -1; Ping = -1;
	}
};

USTRUCT(BlueprintType)
struct FHttpRequest
{

	GENERATED_USTRUCT_BODY()

		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		FString TheDest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		FString TheData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		FString mID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		FString qID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		FString delim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		FString RequestVerb;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		EHttpRequestType RequestType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		FString JsonObjectField;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HTTP Request")
		EHttpResponse ResponseType;
	
	FServerInformation UpdateServer;


	void SetData(FString newData)
	{
		TheData = newData;
	}


	void SetDestination(FString& newDest)
	{
		TheDest = newDest;
	}

	void SetModuleInfo(FString& newMID, FString& newQID)
	{
		mID = newMID;
		qID = newQID;
	}


	void SetRequestType(EHttpRequestType NewRequestType)
	{
		RequestType = NewRequestType;

		switch (RequestType)
		{
		case EHttpRequestType::HRT_ReceiveServerList:
			RequestVerb = "GET";
			TheDest = "/?ReceiveServerList=True";
			break;
		case EHttpRequestType::HRT_RegisterServer:
			RequestVerb = "PUT";
			JsonObjectField = "ServerRegistration";
			break;
		case EHttpRequestType::HRT_UnregisterServer:
			RequestVerb = "PUT";
			JsonObjectField = "ServerDeregistration";
			break;
		case EHttpRequestType::HRT_CheckIn:
			RequestVerb = "PUT";
			JsonObjectField = "ServerCheckIn";
			break;

		}
	}

	FHttpRequest()
	{
		delim = ""; TheDest = ""; TheData = "/?"; mID = ""; qID = ""; RequestVerb = "GET"; RequestType = EHttpRequestType::HRT_None; JsonObjectField = ""; ResponseType = EHttpResponse::HR_None;
	}
};


/**
*
*/
UCLASS(Blueprintable, BlueprintType)
class MASTERSERVER_API UMasterServerFunctions : public UObject
{
	GENERATED_UCLASS_BODY()

private:

	void StartupModule();
	void ShutdownModule();

	UGameInstance* GameInstance;

	FHttpModule* Http;
	FString TargetHost;
	bool bIsBusy;

	/** Delegate for callbacks to Tick */
	FTickerDelegate TickDelegate;
	bool Tick(float DeltaSeconds);

	TArray<FHttpRequest> RequestQueue;
	FHttpRequest CurrentRequest;

	TArray<FServerInformation> ServerList;

	static void StaticHTTPHandler(HttpResponse* Response, void* UserData);
	Httpd* HTTPServer;

	std::clock_t	start;
	float PingElapsedTime;

	void HttpProcess();

	FTimerHandle HttpProcess_Handle;

	void StartTransmission();
	void OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	TArray<uint8> FStringToBinaryArray(FString InString);
	FString BinaryArrayToFString(TArray<uint8> InBinaryArray);

	FString DecompressBytes(TArray<uint8> BinaryArray);
	TArray<uint8> CompressBytes(FString UncompressedString);

	void ProcessJSON(FString JsonString);
	void ProcessText(FString ContentString);

	bool bTransmitSuccessful;
	float CheckInFrequency;
	void ProcessServerList(TSharedPtr<FJsonObject> JsonParsed);
	void Closed(FString Message);

public:

	UFUNCTION(BlueprintCallable, Category = "Networking")
	void Initalize(UGameInstance* NewParent, FString Ip, FString Port);
	UFUNCTION(BlueprintCallable, Category = "Networking")
	void Shutdown();
	UFUNCTION(BlueprintCallable, Category = "Networking")
	void TransmitRequest(FHttpRequest& request);


	// Our event for when we request a new serverlist
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnServerListReceived, EHttpResponse, Response, const TArray<FServerInformation>&, ServerList);
	UPROPERTY(BlueprintAssignable, Category = "Networking Library")	
		FOnServerListReceived ServerListReceivedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnServerRegistered, EHttpResponse, Response, float, CheckInFrequency);
	UPROPERTY(BlueprintAssignable, Category = "Networking Library")
		FOnServerRegistered ServerRegisteredEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerUnregistered, EHttpResponse, Response);
	UPROPERTY(BlueprintAssignable, Category = "Networking Library")
		FOnServerUnregistered ServerUnregisteredEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerCheckIn, EHttpResponse, Response);
	UPROPERTY(BlueprintAssignable, Category = "Networking Library")
		FOnServerCheckIn ServerCheckInEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPingComplete, EHttpResponse, Response, FServerInformation, ServerUpdated);
	UPROPERTY(BlueprintAssignable, Category = "Networking Library")
		FOnPingComplete ServerPingComplete;

	UFUNCTION(BlueprintCallable, Category = "Networking")
		void RequestServerList();

	FServerInformation CurrentRegisteredServer;
	FTimerHandle CheckIn_Handle;


	UFUNCTION()
		void CheckInServer();

	UFUNCTION(BlueprintCallable, Category = "Networking")
		void RegisterServer(FServerInformation Server);

	UFUNCTION()
		void OnRegisteredServer(EHttpResponse Reponse, float CheckInFrequency);

	UFUNCTION(BlueprintCallable, Category = "Networking")
		void UnregisterServer();

	UFUNCTION(BlueprintCallable, Category = "Networking")
		void UpdatePing(FServerInformation Server);

	FString ServerToJSON(FServerInformation InServer, FHttpRequest InRequest);

	// Included so blueprint people can create class
	UFUNCTION(BlueprintPure, meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "Create Object From Blueprint", CompactNodeTitle = "Create", Keywords = "new create blueprint"), Category = Game)
		static UObject* NewObjectFromBlueprint(UObject* WorldContextObject, TSubclassOf<UObject> UC);
};