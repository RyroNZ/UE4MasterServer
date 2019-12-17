/* 
Copyright (c) 2019 Ryan Post

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
*/

#pragma once

#include "CoreMinimal.h"
#include "OnlineAsyncTaskManager.h"

/**
 *	Null version of the async task manager to register the various Python callbacks with the engine
 */
class FOnlineAsyncTaskManagerPython : public FOnlineAsyncTaskManager
{
protected:

	/** Cached reference to the main online subsystem */
	class FOnlineSubsystemPython* NullSubsystem;

public:

	FOnlineAsyncTaskManagerPython(class FOnlineSubsystemPython* InOnlineSubsystem)
		: NullSubsystem(InOnlineSubsystem)
	{
	}

	~FOnlineAsyncTaskManagerPython() 
	{
	}

	// FOnlineAsyncTaskManager
	virtual void OnlineTick() override;
};
