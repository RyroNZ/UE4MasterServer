#pragma once

// Engine Includes
#include "Object.h"
#include "Engine/DeveloperSettings.h"

#include "OnlineSubsystemPythonConfig.generated.h"

/**
* Config class for Epic Online Services settings.
*/
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Online Subsystem Python"))
class ONLINESUBSYSTEMPYTHON_API UOnlineSubsystemPythonConfig : public UObject
{
	GENERATED_BODY()

public:

	UOnlineSubsystemPythonConfig();

	/** Sets the ProductID. */
	void			SetServerAddress(FString NewServerAddress);

	/** Sets the SandboxID. */
	void			SetAuthorizationTicket(FString NewAuthorizationTicket);

	/** The Server to use as the master server (ip:port) for this Project. */
	UPROPERTY(config, EditAnywhere, Category = "EOS")
		FString		ServerAddress;

	/** The Authorization Ticket to pass to the master server for this Project. */
	UPROPERTY(config, EditAnywhere, Category = "EOS")
		FString		AuthorizationTicket;
};
