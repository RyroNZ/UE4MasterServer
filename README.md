# Unreal Engine 4 Master Server

##### Version
15.06.1

This is a plugin for Unreal Engine 4 that adds server registration, deregistration etc with a master server. Once the plugin has been installed you will be able to use this to receive server lists of all active servers with their IP, Port, Name, Game Mode, Map, Current Players, Max Players, and anything else you wish to add :).



## Setup
---
##Plugin Integration


There are a couple of different ways to integrate this plugin into your project. But for our purposes we will go for project integration.

###Code Based Project

This method enables the plugin in a single code-based project. This can be done on any project that was create as a code project (Unfortunately blueprint only projects are **not** supported at this stage. Please note, even with this limitation the plugin can be used purely in blueprint and does not require any C++ knowledge)

1. Clone this repo to a subfolder in your project called /Plugins/MasterServer.
2. Open your project, you will be prompted to rebuild the modules. (YourProjectName-MasterServer.dll)
3. Select Yes to rebuild, this will take a few moments.


###Enabling the Plugin

Ensure the plugin is enabled (by default, this should already be the case)

1. In the editor, select Plugins from the Window menu
2. Scroll down to Project (assuming you opted for project integration) and select Networking
3. Confirmed Enabled is checked.
<br>
<br>

![plugins window](http://ryanpost.me/wp-content/uploads/2015/06/UE4Plugins.png)

##Server Installation

Installing, and running the server will be different depending on your operating sytem, but we will handle this for Windows and Linux.

###Windows Setup
This setup is fairly straight forward if the requirements are met.
1. Clone the repository, or download the zip and extract it some place safe.
2. Open UE4MasterServer/Server/ (or UE4MasterServer-master/Server/ if you extracted the zip)
3. Run the Windows Setup shortcut
4. Start the server with MasterServer.py

####Requirements
* Python 3.4
* setuptools

###Linux Setup

If you meet the requirements, the below is sufficient to install and run the server.
```sh
$ git clone https://github.com/RyroNZ/UE4MasterServer.git
$ cd UE4MasterServer/Server/
$ virtualenv -p /usr/bin/python3.4 py3env
$ source py3env/bin/activate
$ python setup.py install
$ python MasterServer.py
```



####Requirements

* Python 3.4
* pip
* setuptools
* virtualenv

If all goes well, you should see 'Started HTTP server on port xxxx'

##Configuration

Some configuration options are provided as globals in the MasterServer.py, these are as follows;

>PORT
<br> This defines which port the HTTP server will use, whatever this is set to needs to tbe the same as the plugin.
<br>Default=8081

>TICK_FREQUENCY
<br>This defines how frequently the server will process the queues and update the serverlist in seconds. Having a higher value will reduce load, at the cost of the clients potentially having a delayed serverlist.
<br>Default=1

>CHECK_IN_FREQUENCY
<br>This defines how frequently the server is required to check in, to confirm it is still active and to update it's registration in seconds. (ie. player count changes, or map changes)
<br>Default=30

>MISSED_CHECKINS_BEFORE_INACTIVE
<br>This defines how many check ins the server can miss before it is set to inactive and not served up in the serverlist. <br>Default=2

