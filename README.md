# Master Server with Unreal Engine 4 Plugin

##### Version
20.07.02

##### Changelog
###### 16.07.02
* Fixed uplugin engine version.

###### 16.07.02
* Divided time by two to get one way trip time as opposed to round trip, and further improved accuracy (is now very good on accuracy, with 6-15ms added for processing the request)
* Modified time-out for ping request to 1 seconds, and 10 seconds for a standard request.
* Removed some debug code that was left in 16.7.01

###### 16.07.01
* Modified HttpProcess so it is now being called on Tick, instead of by a Timer. Improves latency accuracy and consistency.

<br>
This is a plugin for Unreal Engine 4 that adds super simple server registration, deregistration, etc with a master server. 

This is not mean't as a complete Online Subsystem, just as a way for people with a need of getting an up to date serverlist they can serve up to clients and adapt it to their own needs (I am open to suggestions of functionality you want included by default!).

Once the plugin has been installed you will be able to use this to receive server lists of all active servers with their IP, Port, Name, Game Mode, Map, Current Players, Max Players, and anything else you wish to add as it is free to use and modify in your projects. :).

This is quite basic at this stage, but I will be updating this in the coming weeks to include more features such as pinging servers received on the serverlist, and some functions to get the public IP from the adapter, and possibly expanding it to Unity.


If you have any queries, feedback or concerns please email me at ryan@ryanpost.me.

## Setup
---
<br>
## Plugin Integration


There are a couple of different ways to integrate this plugin into your project. But for our purposes we will go for project integration.



### Code Based Project

This method enables the plugin in a single code-based project. This can be done on any project that was created as a code project (Unfortunately blueprint only projects are **not** supported at this stage. Please note, even with this limitation the plugin can be used purely in blueprint and does not require any C++ knowledge)

1. Clone this repo to a subfolder in your project called /Plugins/MasterServer.
2. Open your project, you will be prompted to rebuild the modules. (YourProjectName-MasterServer.dll)
3. Select Yes to rebuild, this will take a few moments.


### Enabling the Plugin

Ensure the plugin is enabled (by default, this should already be the case)

1. In the editor, select Plugins from the Window menu
2. Scroll down to Project (assuming you opted for project integration) and select Networking
3. Confirmed Enabled is checked.
<br>
<br>

![plugins window](http://ryanpost.me/wp-content/uploads/2015/06/plugins.png)

# Server Installation

Installing, and running the server will be different depending on your operating sytem, but we will handle this for Windows and Linux.

## Windows Setup

This setup is fairly straight forward if the requirements are met.

1. Clone the repository, or download the zip and extract it some place safe.
2. Open UE4MasterServer/Server/ (or UE4MasterServer-master/Server/ if you extracted the zip)
3. Run the Windows Setup shortcut
4. Start the server with MasterServer.py

#### Requirements
* Python 3.4
* setuptools

## Linux Setup

If you meet the requirements, the below is sufficient to install and run the server.

```sh
$ git clone https://github.com/RyroNZ/UE4MasterServer.git
$ cd UE4MasterServer/Server/
$ virtualenv -p /usr/bin/python3.4 py3env
$ source py3env/bin/activate
$ python setup.py install
$ python MasterServer.py
```

#### Requirements

* Python 3.4
* pip
* setuptools
* virtualenv

If all goes well, you should see 'Started HTTP server on port xxxx'

## Configuration
---

## Server
Some configuration options are provided as globals in the MasterServer.py, these are as follows;

>PORT
<br> This defines which port the HTTP server will use, whatever this is set to needs to the the same as the plugin.
<br>Default=8081

>TICK_FREQUENCY
<br>This defines how frequently the server will process the queues and update the serverlist in seconds. Having a higher value will reduce load at the cost of the clients potentially having a outdated serverlist.
<br>Default=1

>CHECK_IN_FREQUENCY
<br>This defines how frequently the server is required to check in to confirm it is still active and to update it's registration in seconds. (ie. player count changes, or map changes). This is a server side value, so any clients using the plugin will reflect this change.
<br>Default=30

>MISSED_CHECKINS_BEFORE_INACTIVE
<br>This defines how many check ins the server can miss before it is set to inactive and not served up in the serverlist. <br>Default=2

>LOGGING
<br>This defines if the server should log requests, and transactions (ie. sending serverlist, registering server, purging server, etc)
<br>Default=True

## Client

We will go through the process to initalize the plugin, register, and deregister a server, and how to handle the various events this plugin provides.

### Blueprint
This plugin is able to be completely used in blueprint, so we will demonstrate how to do this.

Due to some limitations with STRUCTS and blueprint, the functions in the struct are not available. I have included a blueprint helper class under Client/Blueprint/BP_MasterServerHelper.UASSET that recreates these functions.

#### Intialize the plugin
This is what we will need to do before we start anything, it is recommended to do this in the Game Instance class so that it does not get cleaned up during game play (ie. change map and have the client stop ticking).

We need to initalize the Master Server with an IP and PORT that correspond to your server.

![initalize server](http://ryanpost.me/wp-content/uploads/2015/06/init_blueprint.png)

#### Registering a server

Registering a server is very simple. We need to make a up a Server Information object to send to the server. If you wish to do anything with the response you will need to bind to the response before you send the registration. 

You would likely want to get the public interface via some method for the IP (I may include this in the future update).

Once we have received the response, we can do something with the response (maybe re-register if it fails for any reason). If the registration succeeds the client will automitcally tick the required Check In Freqnecy and nothing further needs to be done with this.

![register server](http://ryanpost.me/wp-content/uploads/2015/06/registration_blueprint.png)

#### Unregistering a server

De-registration is very similar to registering a server. We also have the option of binding to an event if we want to handle the response. If this fails, it is not too critical as the check in will also stop and the server will be set to in-active on the master server after a short time.

![deregistration](http://ryanpost.me/wp-content/uploads/2015/06/unregister_blueprint.png)

#### Checking In

Checking in is an automatic process and you do not need to do anything for it, but if you would like to bind an event to the check in response you can (maybe to print a message if the check in is failing, or handle it in some other way so the client knows :) ).

![checkin](http://ryanpost.me/wp-content/uploads/2015/06/checkin_blueprint.png)

#### Requesting the serverlist

Again, a lot like all of the other process. We will likely want to bind to an event to handle this one (ie. populating the server browser).

Once you have the list of servers you may want to use a method of pinging them and confirming they are valid (and so you know you are not joining a high ping server). I may add this functionality in the near future so that the Ping field in the struct will be updated upon receiving the serverlist, but at this stage, that ones up to you to sort out.

![request serverlist](http://ryanpost.me/wp-content/uploads/2015/06/serverlist_request_blueprint.png)

#### Updating Ping for Server

Very simple, and likely somethign you would do straight after requesting the serverlist, and before adding it to the browser. Once the ping is received you could add it to the serverlist. please see the example of how you may want to implement this below.

![ping server](http://ryanpost.me/wp-content/uploads/2015/06/ping_blueprint.png)
### C++

#### Setup Plugin
To enable to plugin functionality on the C++ side of things, we need to add it to YourProject.Build.cs file, and include the headers.

YourProject.Build.cs should be located in YourProject/Source/YourProject/YourProject.Build.CS
Append "MasterServer" to this PublicDependencyModuleNames like below.

```cpp
    public class Code10 : ModuleRules
    {
    	public Code10(TargetInfo Target)
    	{
            PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "MasterServer"});
    	}
    }
```
Include the header in your project

    #include "MasterServerFunctions.h"


####Initalize the plugin

This should ideally be in your GameInstance class.
```cpp
 	MasterServer = NewObject<UMasterServerFunctions>(UMasterServerFunctions::StaticClass());
 	MasterServer->Initalize(this, "127.0.0.1", "8081");
```
#### Register a server
```cpp
 	FServerInformation SomeServer;
 
 	SomeServer.Name = "MyServerName";
 	SomeServer.Ip = "127.0.0.1";
 	SomeServer.Port = "2302";
 	SomeServer.Map = "MyServerMap";
 	SomeServer.GameMode = "MyServerGameModee";
 	SomeServer.CurrentPlayers = 0;
 	SomeServer.MaxPlayers = 10;

    //Bind to the delegate
    MasterServer->ServerRegisteredEvent.AddDynamic(this, &MyGameInstance::OnServerRegistered);
    //Do the registering
 	MasterServer->RegisterServer(SomeServer);
```

#### Unregister a server
```cpp
    //Bind to the delegate
	MasterServer->ServerUnregisteredEvent.AddDynamic(this, &MyGameInstance::OnServerUnregistered);
	//Do the unregistering
	MasterServer->UnregisterServer()
```

#### Checking In

```cpp
    //Bind to the delegate
    MasterServer->ServerCheckInEvent.AddDynamic(this, &MyGameInstance::ServerCheckedIn);
  ```  
#### Requesting the Serverlist

```cpp
    //Bind to the delegate
	MasterServer->ServerListReceivedEvent.AddDynamic(this, &MyGameInstance::OnServerListReceived);
	//Request the serverlist
	MasterServer->RequestServerList();
	

```

#### Pinging a server
```cpp
    //Bind to the delegate
	MasterServer->ServerListReceivedEvent.AddDynamic(this, &MyGameInstance::OnServerListReceived);
	//Request the serverlist
	MasterServer->RequestServerList();
	
	void MyGameInstance::OnServerListReceived(FHttpResponse Response, const TArray<FServerInformation>& Serverlist)
	{
	    //Just received the serverlist
	    for (int32 i = 0; i < ServerList.Num(); i++)
	    {
	        MasterServer->ServerPingComplete.AddDynamic(this, &MyGameInstance::OnPingUpdated)
	        MasterServer->UpdatePing(ServerList[i]);
	    }
	}
	
	void MyGameInstance::OnPingUpdated(FHttpResponse Response, FServerInformation ServerUpdated)
	{
	    //Ping has been updated.. add it to the serverlist, if we cannot ping the server there is some issue and it likely won't actually be running anyway
	}
	
	
```

<br>
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
