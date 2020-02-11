
# Copyright (c) 2019 Ryan Post
# This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.
# Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:
# 1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.



import cherrypy
from cherrypy.lib import auth_digest
from cherrypy import response
import json
import jsonpickle
import time
from time import sleep
from threading import Thread
import requests

class Server(object):
     def __init__(self):
        self.registeredby = ''
        self.name = ''
        self.port = 0
        self.map = ''
        self.playercount = 0
        self.maxplayers = 0
        self.pwprotected = False
        self.gamemode = ''
        self.timeoflastheartbeat = 0
		
     def __eq__(self, other):
        if isinstance(other, self.__class__):
            return self.name== other.name and self.port == other.port
        else:
            return False

class MasterServer(object):

    def __init__(self):
        # Time between heartbeat in seconds, this is passed to the client and kept in sync.
        self.time_between_heartbeats = 30
        self.serverlist = []
        thread = Thread(target = self.heartbeat)
        thread.start()


    def heartbeat(self):
        while True:
            for server in self.serverlist:
                delta = int(time.time()) - server.timeoflastheartbeat
                if (delta > self.time_between_heartbeats):
                    self.serverlist.remove(server)
            sleep(1)
            
    @cherrypy.expose
    def register_server(self, name, port, map, maxplayers, pwprotected, gamemode):
        if self.server_exists(cherrypy.request.remote.ip, port):
            self.internal_update_server(cherrypy.request.remote.ip, port, name, map, 0, maxplayers, pwprotected, gamemode)
            return json.dumps({'error' : False, 'message' : 'Sucessfully updated your server [%s %s:%s] on the server browser.' % (name, cherrypy.request.remote.ip, port)})
        else:
            server = Server()
            server.ip = cherrypy.request.remote.ip
            server.name = name
            server.port = port
            server.map = map
            server.maxplayers = maxplayers
            server.pwprotected = pwprotected
            server.gamemode = gamemode
            server.timeoflastheartbeat = int(time.time())
            try:
                r = requests.get('http://' + cherrypy.request.remote.ip + ':' + port, timeout=2)
            except requests.exceptions.ConnectTimeout:
                # ports are not forwarded...
                if (cherrypy.request.remote.ip != "127.0.0.1"):
                    return json.dumps({'error' : True, 'message' : 'Unable to connect to server [%s %s:%s]. Please verify your ports are forwarded and your firewall is not blocking the game. Your server will not be visible in the Server Browser.' % (server.name, server.ip, server.port)})
            except requests.exceptions.ConnectionError:
                pass

            self.serverlist.append(server)
            return json.dumps({'error' : False, 'message' : 'Sucessfully added your server [%s %s:%s] to the server browser.' % (name, cherrypy.request.remote.ip, port), 'heartbeat' : self.time_between_heartbeats })


    def internal_update_server(self, ip, port, name, map, playercount, maxplayers, pwprotected, gamemode):
        for server in self.serverlist:
            if server.ip == ip and server.port == port: 
                server.name = name
                server.map = map
                server.playercount = playercount
                server.maxplayers = maxplayers
                server.pwprotected = pwprotected
                server.gamemode = gamemode
                server.timeoflastheartbeat = int(time.time())
                return True
        return False

    @cherrypy.expose
    def update_server(self, port, name, map, playercount, maxplayers, pwprotected, gamemode):
        if self.internal_update_server(cherrypy.request.remote.ip, port, name, map, playercount, maxplayers, pwprotected, gamemode):
            return json.dumps({'error' : False, 'message' : 'Sucessfully updated your server [%s %s:%s] on the server browser.' % (name, cherrypy.request.remote.ip, port)})
        return json.dumps({'error' : True, 'message' : 'Server not registered'})
         

    def server_exists(self, ip, port):
        for server in self.serverlist:
            if server.ip == ip and server.port == port:
                return True
        return False

    @cherrypy.expose
    def unregister_server(self, port):
        for server in self.serverlist:
            if (server.registeredby == cherrypy.request.remote.ip and server.port == port):
                self.serverlist.remove(server)

    @cherrypy.expose
    def get_serverlist(self):
        self.returnlist = []
        for server in self.serverlist:
            jsonstring = {'name' : server.name, 'port' : server.port, 'map' : server.map, 'playercount' : server.playercount, 'maxplayers' : server.maxplayers, 'pwprotected' : server.pwprotected, 'gamemode' : server.gamemode, 'ip' : server.ip }
            self.returnlist.append(jsonstring)
        return json.dumps({'error' : False, 'message' : '', 'servers' : self.returnlist})

    @cherrypy.expose
    def perform_heartbeat(self, port):
        for server in self.serverlist:
            if (server.ip == cherrypy.request.remote.ip and server.port == port):
                server.timeoflastheartbeat = int(time.time())





cherrypy.config.update({ 'server.socket_port': 8081,
                         'server.socket_host': '0.0.0.0',
                         "server.ssl_module": "pyopenssl",
                         'server.thread_pool' : 100
                       })

masterserver = MasterServer()
cherrypy.quickstart(masterserver)
