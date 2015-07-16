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

You should have received a copy of the GNU General PublicLicense
along with this program.If not, see < http://www.gnu.org/licenses/>.

*/

#include "MasterServerPrivatePCH.h"
#include "MasterServerFunctions.h"

UMasterServerFunctions::UMasterServerFunctions(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	UMasterServerFunctions::StaticClass();
}

void UMasterServerFunctions::Initalize(UGameInstance* NewParent, FString Ip, FString Port)
{
	Http = &FHttpModule::Get();
	TargetHost = FString::Printf(TEXT("http://%s:%s"), *Ip, *Port);
	GameInstance = NewParent;

	HTTPServer = httpd_create(8082, StaticHTTPHandler, this);

	// Register delegate for ticker callback
	TickDelegate = FTickerDelegate::CreateUObject(this, &UMasterServerFunctions::Tick);
	FTicker::GetCoreTicker().AddTicker(TickDelegate);
}

bool UMasterServerFunctions::Tick(float DeltaSeconds)
{
	HttpProcess();

	return true;
}

void UMasterServerFunctions::Shutdown()
{
	if (HTTPServer)
	{
		httpd_destroy(HTTPServer);
	}
}

void UMasterServerFunctions::HttpProcess()
{
	if (HTTPServer)
	{
		httpd_process(HTTPServer, false);
	}
}

void UMasterServerFunctions::StaticHTTPHandler(HttpResponse* Response, void* UserData)
{
	httpresponse_response(Response, 200, "Request Received!", 0, "Content-Type: text\r\n");
}


void UMasterServerFunctions::TransmitRequest(FHttpRequest& request)
{
	RequestQueue.Add(request);
	if (!bIsBusy)
	{
		StartTransmission();
	}
}



void UMasterServerFunctions::StartTransmission()
{
	if (RequestQueue.Num() <= 0)
	{
		return;
	}

	bIsBusy = true;
	CurrentRequest = RequestQueue[0];
	RequestQueue.RemoveAt(0);



	if (!Http) return;
	if (!Http->IsHttpEnabled()) return;

	TSharedRef < IHttpRequest > Request = Http->CreateRequest();
	Request->SetVerb(CurrentRequest.RequestVerb);
	Request->SetHeader("User-Agent", "UE4GameClient/1.0");


	if (CurrentRequest.RequestType == EHttpRequestType::HRT_PingServer)
	{
		start = std::clock();

		//URL should be the server we are trying to ping
		Request->SetURL("http://" + CurrentRequest.UpdateServer.Ip + ":" + FString::SanitizeFloat(8082));
	}
	else
	{
		Request->SetHeader("Content-Type", "application/json");
		Request->SetURL(TargetHost + CurrentRequest.TheDest);
		Request->SetContent(CompressBytes(CurrentRequest.TheData));
	}

	ServerList.Empty();

	Request->OnProcessRequestComplete().BindUObject(this, &UMasterServerFunctions::OnResponseReceived);
	Request->ProcessRequest();

}

void UMasterServerFunctions::OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	FString MessageBody = "";

	if (!Response.IsValid()) { CurrentRequest.ResponseType = EHttpResponse::HR_NoData; Closed(MessageBody); return; }
	if (EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{

		// If we want to ping the server only, get the time now, as we do not want it to include processing and broadcast time
		if (CurrentRequest.RequestType == EHttpRequestType::HRT_PingServer)
		{
			CurrentRequest.ResponseType = EHttpResponse::HR_Success;
			CurrentRequest.UpdateServer.Ping = std::clock() - start / (double)(CLOCKS_PER_SEC / 1000);
		}

		if (Response->GetContentType().Equals("application/json"))
		{
			MessageBody = DecompressBytes(Response->GetContent());
			ProcessJSON(MessageBody);
		}
	}
	else
	{
		MessageBody = FString::Printf(TEXT("{\"success\":\"HTTP Error: %d\"}"), Response->GetResponseCode());
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::MakeRandomColor(), FString::SanitizeFloat(Response->GetResponseCode()));
	}

	Closed(MessageBody);
}

TArray<uint8> UMasterServerFunctions::FStringToBinaryArray(FString InString)
{
	TArray<uint8> BinaryArray;

	for (int32 i = 0; i < InString.Len(); i++)
	{
		BinaryArray.Add(InString[i]);
	}
	return BinaryArray;
}

FString UMasterServerFunctions::BinaryArrayToFString(TArray<uint8> InBinaryArray)
{
	return FString(UTF8_TO_TCHAR(InBinaryArray.GetData()));
}

FString UMasterServerFunctions::DecompressBytes(TArray<uint8> CompressedBinaryArray)
{
	TArray<uint8> UncompressedBinaryArray;
	UncompressedBinaryArray.SetNum(CompressedBinaryArray.Num() * 1032);

	//int ret;
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;

	strm.avail_in = CompressedBinaryArray.Num();
	strm.next_in = (Bytef *)CompressedBinaryArray.GetData();
	strm.avail_out = UncompressedBinaryArray.Num();
	strm.next_out = (Bytef *)UncompressedBinaryArray.GetData();

	// the actual DE-compression work.
	inflateInit(&strm);
	inflate(&strm, Z_FINISH);
	inflateEnd(&strm);

	return BinaryArrayToFString(UncompressedBinaryArray);
}

TArray<uint8> UMasterServerFunctions::CompressBytes(FString UncompressedString)
{
	TArray<uint8> UncompressedBinaryArray = FStringToBinaryArray(UncompressedString);
	TArray<uint8> CompressedBinaryArray;
	CompressedBinaryArray.SetNum(UncompressedBinaryArray.Num() * 1023, true);

	//int ret;
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;

	strm.avail_in = UncompressedBinaryArray.Num();
	strm.next_in = (Bytef *)UncompressedBinaryArray.GetData();
	strm.avail_out = CompressedBinaryArray.Num();
	strm.next_out = (Bytef *)CompressedBinaryArray.GetData();


	// the actual compression work.
	deflateInit(&strm, Z_BEST_COMPRESSION);
	deflate(&strm, Z_FINISH);
	deflateEnd(&strm);

	// Shrink the array to minimum size
	CompressedBinaryArray.RemoveAt(strm.total_out, CompressedBinaryArray.Num() - strm.total_out, true);
	return CompressedBinaryArray;

}

void UMasterServerFunctions::ProcessJSON(FString JsonString)
{
	TSharedPtr<FJsonObject> JsonParsed = MakeShareable(new FJsonObject());
	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonString);

	bool ReadSuccess = FJsonSerializer::Deserialize(JsonReader, JsonParsed);
	if (ReadSuccess)
	{
		CurrentRequest.ResponseType = EHttpResponse::HR_Success;
		switch (CurrentRequest.RequestType)
		{
		case EHttpRequestType::HRT_ReceiveServerList:
			ProcessServerList(JsonParsed);
			break;
		case EHttpRequestType::HRT_RegisterServer:
			CheckInFrequency = JsonParsed->GetNumberField("CheckInFrequency");
		}
	}
	else
	{
		CurrentRequest.ResponseType = EHttpResponse::HR_ParseError;
	}
}


void UMasterServerFunctions::ProcessServerList(TSharedPtr<FJsonObject> JsonParsed)
{
	TArray<TSharedPtr<FJsonValue>> JsonArrays = JsonParsed->GetArrayField("servers");
	for (int32 i = 0; i < JsonArrays.Num(); i++)
	{
		TSharedPtr<FJsonObject> JsonArray = JsonArrays[i]->AsObject();
		// For each server we received, store it's information
		FServerInformation Server;

		Server.GetDataFromJSON(JsonArray);
		ServerList.Add(Server);
	}

}

void UMasterServerFunctions::Closed(FString Message)
{
	// Broadcast our serverlist to any delegates subscribed to this event. Functions should only be subscribed to the event when they are waiting for data.

	switch (CurrentRequest.RequestType) {

	case EHttpRequestType::HRT_ReceiveServerList:
		ServerListReceivedEvent.Broadcast(CurrentRequest.ResponseType, ServerList);
		break;
	case EHttpRequestType::HRT_RegisterServer:
		ServerRegisteredEvent.Broadcast(CurrentRequest.ResponseType, CheckInFrequency);
		break;
	case EHttpRequestType::HRT_CheckIn:
		ServerCheckInEvent.Broadcast(CurrentRequest.ResponseType);
		break;
	case EHttpRequestType::HRT_UnregisterServer:
		ServerUnregisteredEvent.Broadcast(CurrentRequest.ResponseType);
		break;
	case EHttpRequestType::HRT_PingServer:
		ServerPingComplete.Broadcast(CurrentRequest.ResponseType, CurrentRequest.UpdateServer);

	}

	//Reset our checks
	bTransmitSuccessful = false;

	//Decode(Message);
	bIsBusy = false;

	//Handle anything else in the queue
	StartTransmission();
}


void UMasterServerFunctions::RequestServerList()
{
	FHttpRequest* RequestData = new FHttpRequest;
	RequestData->SetRequestType(EHttpRequestType::HRT_ReceiveServerList);
	TransmitRequest(*RequestData);
}


void UMasterServerFunctions::UnregisterServer()
{
	if (!CurrentRegisteredServer.IsEmpty())
	{

		// Clear the timer on checking in as we don't need to check in anymore
		GameInstance->GetWorld()->GetTimerManager().ClearTimer(CheckIn_Handle);

		FHttpRequest* RequestData = new FHttpRequest;

		RequestData->SetRequestType(EHttpRequestType::HRT_UnregisterServer);
		RequestData->SetData(ServerToJSON(CurrentRegisteredServer, *RequestData));

		// No server currently registered anymore, if the request fails, at least check in won't happen and the server will be purged automatically after a short period of time.
		CurrentRegisteredServer = CurrentRegisteredServer = *(new FServerInformation);

		TransmitRequest(*RequestData);
	}
	else
	{
		// No server registered, broadcast failure
		ServerUnregisteredEvent.Broadcast(EHttpResponse::HR_NoSend);
	}

}

void UMasterServerFunctions::UpdatePing(FServerInformation Server)
{
	FHttpRequest* RequestData = new FHttpRequest;
	RequestData->SetRequestType(EHttpRequestType::HRT_PingServer);
	RequestData->UpdateServer = Server;
	TransmitRequest(*RequestData);
}

void UMasterServerFunctions::CheckInServer()
{
	if (!CurrentRegisteredServer.IsEmpty())
	{

		FHttpRequest* RequestData = new FHttpRequest;
		RequestData->SetRequestType(EHttpRequestType::HRT_CheckIn);
		RequestData->SetData(ServerToJSON(CurrentRegisteredServer, *RequestData));

		TransmitRequest(*RequestData);
	}
	else
	{
		ServerCheckInEvent.Broadcast(EHttpResponse::HR_NoSend);
	}

}

void UMasterServerFunctions::RegisterServer(FServerInformation Server)
{
	if (!Server.IsEmpty())
	{
		ServerRegisteredEvent.RemoveDynamic(this, &UMasterServerFunctions::OnRegisteredServer);
		ServerRegisteredEvent.AddDynamic(this, &UMasterServerFunctions::OnRegisteredServer);

		FHttpRequest* RequestData = new FHttpRequest;
		RequestData->SetRequestType(EHttpRequestType::HRT_RegisterServer);
		RequestData->SetData(ServerToJSON(Server, *RequestData));
		CurrentRegisteredServer = Server;

		TransmitRequest(*RequestData);
	}
	else
	{
		ServerRegisteredEvent.Broadcast(EHttpResponse::HR_NoSend, 0);
	}

}

void UMasterServerFunctions::OnRegisteredServer(EHttpResponse Response, float CheckInFrequency)
{
	if (Response == EHttpResponse::HR_Success)
	{
		// Start checking in with the master server browser
		GameInstance->GetWorld()->GetTimerManager().SetTimer(CheckIn_Handle, this, &UMasterServerFunctions::CheckInServer, CheckInFrequency, true);
	}
	else
	{
		// Set our currently registered server to an empty server because it failed...
		CurrentRegisteredServer = *(new FServerInformation);
	}
}

FString UMasterServerFunctions::ServerToJSON(FServerInformation InServer, FHttpRequest InRequest)
{
	TSharedPtr<FJsonObject> ServerJSONArray = MakeShareable(new FJsonObject());
	ServerJSONArray->SetObjectField(InRequest.JsonObjectField, InServer.GenerateJSONObject());
	FString OutputString;
	TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(ServerJSONArray.ToSharedRef(), Writer);

	return OutputString;
}

UObject* UMasterServerFunctions::NewObjectFromBlueprint(UObject* WorldContextObject, TSubclassOf<UObject> UC)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject);
	UObject* tempObject = NewObject<UMasterServerFunctions>(UC->StaticClass());

	return tempObject;
}

