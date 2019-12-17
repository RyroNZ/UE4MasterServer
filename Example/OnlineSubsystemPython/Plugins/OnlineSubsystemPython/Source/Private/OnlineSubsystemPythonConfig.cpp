
#include "OnlineSubsystemPythonConfig.h"

UOnlineSubsystemPythonConfig::UOnlineSubsystemPythonConfig()
	: ServerAddress("127.0.0.1:8081")
	, AuthorizationTicket("")
{

}

void UOnlineSubsystemPythonConfig::SetServerAddress(FString NewServerAddress)
{
	ServerAddress = NewServerAddress;
	SaveConfig();
}

void UOnlineSubsystemPythonConfig::SetAuthorizationTicket(FString NewAuthorizationTicket)
{
	AuthorizationTicket = NewAuthorizationTicket;
	SaveConfig();
}