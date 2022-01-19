/* 
Copyright (c) 2019 Ryan Post

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
*/

#include "OnlineSubsystemPythonModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemPython.h"
#include "OnlineSubsystemPythonConfig.h"
#include "UObject/UObjectBase.h"
#if WITH_EDITOR
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "ISettingsContainer.h"
#endif

IMPLEMENT_MODULE(FOnlineSubsystemPythonModule, OnlineSubsystemPython);

#define LOCTEXT_NAMESPACE "FOnlineSubsystemPython"

/**
 * Class responsible for creating instance(s) of the subsystem
 */
class FOnlineFactoryPython : public IOnlineFactory
{
public:

	FOnlineFactoryPython() {}
	virtual ~FOnlineFactoryPython() {}

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName)
	{
		FOnlineSubsystemPythonPtr OnlineSub = MakeShared<FOnlineSubsystemPython, ESPMode::ThreadSafe>(InstanceName);
		if (OnlineSub->IsEnabled())
		{
			if(!OnlineSub->Init())
			{
				UE_LOG_ONLINE(Warning, TEXT("Null API failed to initialize!"));
				OnlineSub->Shutdown();
				OnlineSub = NULL;
			}
		}
		else
		{
			UE_LOG_ONLINE(Warning, TEXT("Null API disabled!"));
			OnlineSub->Shutdown();
			OnlineSub = NULL;
		}

		return OnlineSub;
	}
};

void FOnlineSubsystemPythonModule::StartupModule()
{
	UE_LOG_ONLINE(Warning, TEXT("Python Online Subsystem has started!"));
	PythonFactory = new FOnlineFactoryPython();

	// Create and register our singleton factory with the main online subsystem for easy access
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.RegisterPlatformService("Python", PythonFactory);

}

void FOnlineSubsystemPythonModule::ShutdownModule()
{
	UE_LOG_ONLINE(Warning, TEXT("Python Online Subsystem has shutdown!"));
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.UnregisterPlatformService("Python");
	
	delete PythonFactory;
	PythonFactory = NULL;
}

bool FOnlineSubsystemPythonModule::HandleSettingsSaved()
{
	UOnlineSubsystemPythonConfig* config = GetMutableDefault<UOnlineSubsystemPythonConfig>();

	bool ResaveSettings = true;// false;

	// You can put any validation code in here and resave the settings in case an invalid
	// value has been entered

	if (ResaveSettings)
	{
		config->SaveConfig();
	}
	return true;

}
