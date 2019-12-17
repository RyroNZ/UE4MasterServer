# Online Subsystem Python
Created by RyroNZ

Online Subsystem Python is a self-hosted online subsystem for Creating, Finding and Joining Sessions using the standard session nodes. 
It is based off the Null subsystem.

![](https://puu.sh/El2ZS/e3abeb7e11.png)

Example Video: https://puu.sh/EkmSl/d85c851ec2.mp4

## Installing the Plugin

Copy the Online Subsystem Python folder containing the .uplugin into the Plugins folder in your Project Directory.
If the Plugins folder does not exist you can create it.
Open your .uproject and add the following add “OnlineSubsystemPython” to the plugins list and set it to Enabled.
```
{
    "FileVersion": 3,
    "EngineAssociation": "4.22",
    "Category": "",
    "Description": "",
    "Modules": [
        {
            "Name": "MyProjectName",
            "Type": "Runtime",
            "LoadingPhase": "Default",
            "AdditionalDependencies": [
                "FunctionalTesting"
            ]
        }
    ],
    "Plugins": [
        {
            "Name": "OnlineSubsystemPython",
            "Enabled": true
        }
    ]
}
```
In your DefaultEngine.ini under [OnlineSubsystem] change the DefaultPlatformService so it reads
‘DefaultPlatformService=Python’
```
[OnlineSubsystem]
DefaultPlatformService=Python
bUseBuildIdOverride=true
BuildIdOverride=262494
PollingIntervalInMs=20
bHasVoiceEnabled=true
```

## Running the Server

Find the Server folder in the Plugin directory, copy that to the machine you want to run the server on
(We use Digital Ocean to host our servers, YMMV)


Install Python 3
```
$ python -m pip install --upgrade -r requirements.txt
$ python3 OnlineSubsystemPythonServer.py
```
Your server should be up and running, if you wish to configure the port the server is running on you’ll find it
specified in cherrypy.config.update() on line 125.
```
cherrypy.config.update({ 'server.socket_port': 8081,
                         'server.socket_host': '0.0.0.0',
                         "server.ssl_module": "pyopenssl",
                         'server.thread_pool' : 100
                        })
```

## Configuring Server and Client


Open OnlineSubsystemPythonConfig.cpp in the Source/Private directory

Change the SeverAddress to the IP/Domain you are using for your server.
```
UOnlineSubsystemPythonConfig::UOnlineSubsystemPythonConfig()
    : ServerAddress("127.0.0.1:8081")
    , AuthorizationTicket("")
{}
```

Please note, the plugin only has support for the following session settings key/value pairs.
```
SERVERNAME
MAPNAME
GAMEMODE
PASSWORDPROTECTED
PLAYERCOUNT
```

Everything should be working now and you should be able to host and joining using the standard session nodes!

If there are things not working, please email me at ryan@somethinglogical.co.nz