import os.path
from re import finditer

from tunnel import Tunnel, TunnelStatus
from multicloud import Multicloud

import docker

IPSEC_CONTAINER_NAMES = [r'strongswan_strongswan_1']

STRONGSWAN_DIRECTORY = '/etc/multicloud/strongswan/'


class IPSec(Tunnel):
    def __init__(self, logger):
        Multicloud.__init__(self, logger)

        self.names = IPSEC_CONTAINER_NAMES

    def get_interfaces(self):
        ret = {}
        connections = self.get_connections().keys()

        for connection in connections:
            octets = connection.split('.')
            octets = [hex(int(item))[2:].upper() for item in octets]

            ret[connection] = "vti{}".format('-'.join(octets))

        return ret

    def get_client_connections(self):
        filename = os.path.join(STRONGSWAN_DIRECTORY, "conn/conn.conf")
        try:
            with open(filename, 'r') as file:
                config = file.read()
        except (IOError, OSError):
            return []  # No connections found

        re = finditer(r'conn (\d+\.\d+\.\d+\.\d+)', config)

        conns = []
        for match in re:
            conns.append(match.group(1))

        return conns

    def get_connections(self):
        client = docker.from_env()

        exc = client.exec_create(self.names[0], ' '.join(["strongswan", "status"]))
        stdout = client.exec_start(exc)

        re = finditer(r'ESTABLISHED (.+),.*\.\.(\d+\.\d+\.\d+\.\d+)\[', stdout)

        return {
            match.group(2): match.group(1) for match in re
        }

    def get_tunnel_status(self):
        online = self.get_connections()
        servers = self.get_client_connections()
        offline = [server for server in servers if server not in online.keys()]

        tunnels = [TunnelStatus(
            peer=conn, since=since, is_up=True) for (conn, since) in online.iteritems()]
        tunnels += [TunnelStatus(peer=conn, since=None, is_up=False) for conn in offline]

        return tunnels
