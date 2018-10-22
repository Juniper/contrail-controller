import os
from re import search

import netifaces

from tunnel import Tunnel, TunnelStatus
from multicloud import Multicloud

OPENVPN_CONTAINER_NAMES = [r'openvpn_server_1', r'openvpn_client_[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+_1']

OPENVPN_DIRECTORY = '/etc/multicloud/openvpn/'


class OpenVPN(Tunnel):
    def __init__(self, logger):
        Multicloud.__init__(self, logger)

        self.names = OPENVPN_CONTAINER_NAMES

    def get_ifaces(self):
        ret = {}

        dirname = os.path.join(OPENVPN_DIRECTORY, "conn")

        try:
            files = os.listdir(dirname)
        except (IOError, OSError):
            return {}  # No connections

        for filename in files:
            with open(os.path.join(dirname, filename)) as f:
                contents = f.read()

            ip_re = search(r'remote (.+) \d+', contents)
            iface_re = search(r'dev (.*)', contents)

            if not ip_re or not iface_re:
                continue

            ret[ip_re.group(1)] = iface_re.group(1)

        return ret

    def get_tunnel_status(self):
        tunnels = []

        ifaces = self.get_ifaces()

        for iface in ifaces:
            if iface[1] in netifaces.interfaces():
                tunnels.append(TunnelStatus(peer=iface[0], is_up=True, since=None))
            else:
                tunnels.append(TunnelStatus(peer=iface[0], is_up=False, since=None))

        return tunnels
