'''
                    RyroNZ's Master Server for UE4 v15.06.01

Master Server database designed to interface with RyroNZ's UE4 Networking Plugin
Copyright (C) 2015  Ryan Post

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

'''

import http.server
import socketserver
import json
import peewee
import zlib
import threading
import string
from datetime import *
from urllib import parse
from flask_peewee.utils import get_dictionary_from_model
from peewee import *

# How often the server ticks per second,
# each tick should process any data in the queues and generate a server list if required
TICK_FREQUENCY = 1
PORT = 8081

# How often registered servers should be required to check in
CHECK_IN_FREQUENCY = 30

server_db = SqliteDatabase('server.db')
log_db = SqliteDatabase('log.db')

# Incoming JSON request type dictionary fields

RECEIVE_SERVERLIST = "ReceiveServerList"
SERVER_REGISTRATION = "ServerRegistration"
SERVER_CHECKIN = "ServerCheckIn"
SERVER_DEREGISTRATION = "ServerDeregistration"

# Incoming JSON server dictionary fields
SERVER_ID = "id"
SERVER_REGISTRATION_TIME = "registration_time"
SERVER_NAME = 'name'
SERVER_IP = "ip"
SERVER_PORT = "port"
SERVER_GAMEMODE = "game_mode"
SERVER_MAP = "map"
SERVER_MAXPLAYERS = "max_players"
SERVER_CURRENTPLAYERS = "current_players"
SERVER_ISACTIVE = 'is_active'
SERVER_CLIENTADDR = "client_address"


class Server(Model):
    id = BigIntegerField(unique=True)
    registration_time = DoubleField(default=datetime.now(timezone.utc).timestamp())
    name = CharField()
    ip = CharField()
    port = CharField()
    game_mode = CharField()
    map = CharField()
    max_players = IntegerField()
    current_players = IntegerField()
    is_active = BooleanField(default=True)
    client_address = CharField()

    # Generate a simple ID by adding IP and Port, just so we can update records that already exists easily enough.
    def generate_id(ip, port):
        exclude = set(string.digits)
        id_string = ip + port
        id_string = ''.join(ch for ch in id_string if ch in exclude)
        return int(id_string)

    class Meta:
        database = server_db


class Log(Model):
    time = DateTimeField(default=datetime.now(timezone.utc).now())
    type = CharField()
    message = CharField()
    client = CharField(null=True)

    class Meta:
        database = log_db


class DatetimeEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, datetime):
            return obj.strftime('%Y-%m-%dT%H:%M:%SZ')
        elif isinstance(obj, date):
            return obj.strftime('%Y-%m-%d')
        # Let the base class default method raise the TypeError
        return json.JSONEncoder.default(self, obj)


class MyServer(http.server.BaseHTTPRequestHandler):
    def __init__(self, request, client_address, server):
        # Reference to our main thread that will process any queues.
        global main_thread
        super().__init__(request, client_address, server)

    def log_message(self, format, *args):
        # Don't print a message to screen of each connection
        return

    def get_formatted_ip(self):
        return self.client_address[0] + ":" + str(self.client_address[1])

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "application/json")
        self.end_headers()

        query = parse.urlparse(self.path).query
        if query is not None:
            try:
                query_components = dict(qc.split("=") for qc in query.split("&"))

                if RECEIVE_SERVERLIST in query_components:
                    self.handle_send_serverlist()
            except ValueError:
                self.wfile.write(bytes("Malformed URL", "utf-8"))

    # Client has requested the server list, send this.
    def handle_send_serverlist(self):
        main_thread.queue_log(type="Information",
                                   message="Sending serverlist to " + self.get_formatted_ip(),
                                   client=self.client_address)
        self.wfile.write(main_thread.latest_serverlist)

    def do_PUT(self):
        self.send_response(200)
        self.send_header("Content-type", "application/json")
        self.end_headers()

        if self.headers["content-type"] == "application/json":
            self.process_json(self.rfile.read(int(self.headers["content-length"])))

    def process_json(self, in_compressed_data):
        decompressed_data = zlib.decompress(in_compressed_data).decode()
        json_data = json.loads(decompressed_data)

        was_successful = True
        # We should queue the request to the main thread so that our response is always quick.
        # If the server is slow due to load this should not effect the latency of the responses.
        if SERVER_REGISTRATION in json_data:
            main_thread.queue_server_registration(json_data[SERVER_REGISTRATION], self.client_address)
        elif SERVER_CHECKIN in json_data:
            main_thread.queue_server_checkin(json_data[SERVER_CHECKIN], self.client_address)
        elif SERVER_DEREGISTRATION in json_data:
            main_thread.queue_server_unregister(json_data[SERVER_DEREGISTRATION], self.client_address)
        else:
            # Malformed data or bad request.
            was_successful = False

        reply = zlib.compress(
            json.dumps({"WasSuccessful": was_successful, "CheckInFrequency": CHECK_IN_FREQUENCY},
                       cls=DatetimeEncoder).encode())
        # Reply with the status of the request and the servers required check in frequency,
        # if True we have just added their request to the processing queue,
        # although it doesn't mean it will necessarily succeed.
        self.wfile.write(bytes(reply))


class MainThread():
    def __init__(self):

        # Our latest serverlist that we generate and compress each tme there is a change
        # (ie. new server registered, server has checked in )
        self.latest_serverlist = bytes()

        # Various queues to process on each tick. Pretty self explanatory.
        self.server_registration_queue = []
        self.server_checkin_queue = []
        self.server_deregistration_queue = []
        self.log_queue = []

        # Stops the timer if we are trying to stop the thread.
        # This is usually set when we are trying to stop the server.
        self.waiting_to_stop = False

        # If True, we should generate a new serverlist. Otherwise, data is current.
        self.regenerate_serverlist = False

        # Open the database for writing, set inactive any servers that have expired and generate a serverlist
        self.server_database = server_db
        self.log_database = log_db

        self.open_database()
        self.set_expired_servers_inactive()
        self.generate_latest_serverlist()

    def open_database(self):
        self.server_database.connect()
        Server.create_table(True)

        self.log_database.connect()
        Log.create_table(True)

    def tick_forever(self):
        # Tick X times a second
        with lock:
            if not self.waiting_to_stop:
                threading.Timer(TICK_FREQUENCY, self.tick_forever).start()

                #Process queues
                self.server_registration()
                self.server_checkin()
                self.server_deregistration()
                self.set_expired_servers_inactive()
                self.log_to_database()

                if self.regenerate_serverlist:
                    self.regenerate_serverlist = False
                    self.generate_latest_serverlist()

    def stop_ticking(self):
        self.waiting_to_stop = True

    def generate_latest_serverlist(self):
        servers = []
        try:
            # Get dictionary of servers from database
            serverlist_dict = map(get_dictionary_from_model, Server.select())
            for server in serverlist_dict:
                # If server is active, we should add it.
                if server[SERVER_ISACTIVE]:
                    servers.append(server)

            # Compresses the serverlist for sending to clients.
            self.latest_serverlist = zlib.compress(json.dumps({"servers": servers}, cls=DatetimeEncoder).encode())
            self.queue_log(type="Information", message="Generated server list for " + str(len(servers)) + " servers.")
        except peewee.OperationalError:
            self.queue_log(type="Information", message="No data to generate for servers.")

    # Register a new server in the database, if already exists, then update existing record and set to active.
    def server_registration(self):
        registered_count = 0
        with self.server_database.atomic():
            for server in self.server_registration_queue:
                # Generate a simple server id (for tracking purposes only), as well as last registration_time and set it to active
                server[SERVER_ID] = Server.generate_id(server['ip'], server['port'])
                server[SERVER_REGISTRATION_TIME] = datetime.now(timezone.utc).timestamp()
                server[SERVER_ISACTIVE] = True
                try:
                    Server.create(**server)
                except peewee.IntegrityError:
                    # Server already registered, set is_active to true and update any other information
                    Server.update(id=server[SERVER_ID],
                                  is_active=server[SERVER_ISACTIVE],
                                  registration_time=server[SERVER_REGISTRATION_TIME],
                                  game_mode=server[SERVER_GAMEMODE],
                                  map=server[SERVER_MAP],
                                  max_players=server[SERVER_MAXPLAYERS],
                                  current_players=server[SERVER_CURRENTPLAYERS]
                                  ).where(Server.id == Server.generate_id(server[SERVER_IP], server[SERVER_PORT]))\
                        .execute()

                self.queue_log(type="Information",
                               message="Registered " + server[SERVER_NAME] + " on " + server[SERVER_IP] + ":" + server[
                                   SERVER_PORT],
                               client=server[SERVER_CLIENTADDR])

                registered_count += 1
                self.server_registration_queue.remove(server)

        if registered_count > 0:
            # if we have registered a new server, we need to re do the server list
            self.regenerate_serverlist = True

    # De-register an existing server, effectively sets it to in-active and does not send it in a server list request.
    # De-registered servers are kept in the database for historic reasons.
    def server_deregistration(self):
        servers_unregistered = 0
        with self.server_database.atomic():
            for server in self.server_deregistration_queue:
                servers_unregistered += Server.update(is_active=False).where(
                    Server.id == Server.generate_id(server[SERVER_IP],
                                                    server[SERVER_PORT])).execute()
                self.queue_log(type="Information",
                               message="De-registered " + server[SERVER_NAME] + " on " + server[SERVER_IP] + ":" +
                                       server[SERVER_PORT], client=server[SERVER_CLIENTADDR])
                self.server_deregistration_queue.remove(server)

        if servers_unregistered > 0:
            self.regenerate_serverlist = True

    # Processes the check in queue, if a server fails to check in x times, then it will be set to in-active.
    def server_checkin(self):
        servers_checked_in = 0
        with self.server_database.atomic():
            for server in self.server_checkin_queue:
                server[SERVER_REGISTRATION_TIME] = datetime.now(timezone.utc).timestamp()
                server[SERVER_ISACTIVE] = True
                servers_checked_in += Server.update(registration_time=server[SERVER_REGISTRATION_TIME],
                                                    name=server[SERVER_NAME],
                                                    ip=server[SERVER_IP],
                                                    game_mode=server[SERVER_GAMEMODE],
                                                    map=server[SERVER_MAP],
                                                    max_players=server[SERVER_MAXPLAYERS],
                                                    current_players=server[SERVER_CURRENTPLAYERS]
                                                    ).where(
                    Server.id == Server.generate_id(server[SERVER_IP], server[SERVER_PORT])).execute()
                self.queue_log(type="Information",
                               message=server[SERVER_NAME] + " on " + server[SERVER_IP] + ":" +
                                       server['port'] + " checked in", client=server[SERVER_CLIENTADDR])
                self.server_checkin_queue.remove(server)

        if (servers_checked_in > 0):
            self.regenerate_serverlist = True

    # Set expired servers to not active, and do not send them in a server list request.
    # Servers are kept for historical reasons.
    def set_expired_servers_inactive(self):
        now = datetime.now(timezone.utc).timestamp()
        expired_servers = Server.update(is_active=False).where(
            (Server.registration_time + CHECK_IN_FREQUENCY * 2 < now) & (Server.is_active == True)).execute()
        if expired_servers > 0:
            self.queue_log(type="Information", message="Purged " + str(expired_servers) + " expired servers.")
            self.regenerate_serverlist = True

    # Process the log queue, and write to database.
    def log_to_database(self):
        with self.log_database.atomic():
            for log in self.log_queue:
                log['time'] = datetime.utcnow()
                print(str(log['client']) + "    " + log['message'])
                Log.create(**log)
                self.log_queue.remove(log)

    # Functions for appending data to various queues that are processed each tick.
    def queue_server_registration(self, server, client_address):
        server['client_address'] = client_address
        self.server_registration_queue.append(server)

    def queue_server_checkin(self, server, client_address):
        server['client_address'] = client_address
        self.server_checkin_queue.append(server)

    def queue_server_unregister(self, server, client_address):
        server['client_address'] = client_address
        self.server_deregistration_queue.append(server)

    def queue_log(self, type, message, client=None):
        self.log_queue.append({"type": type, "message": message, "client": client})


try:
    lock = threading.Lock()
    httpd = socketserver.TCPServer(("", PORT), MyServer)
    httpd.allow_reuse_address = True
    print("Started HTTP server on port ", PORT)

    main_thread = MainThread()
    main_thread.tick_forever()

    httpd.serve_forever()
except KeyboardInterrupt:
    print('^C received, shutting down the web server')
    httpd.server_close()
    main_thread.stop_ticking()
