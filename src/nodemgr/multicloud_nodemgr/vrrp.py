from collections import namedtuple
import os.path
from re import finditer, search, MULTILINE

import netifaces

from multicloud import Multicloud

VRRP_CONTAINER_NAMES = [r'vrrp_vrrp_1']
VRRP_DIRECTORY = '/etc/multicloud/vrrp'

HAStatus = namedtuple('HAStatus', ['network', 'master_status'])


class VRRP(Multicloud):
    def get_markers(self):
        path = os.path.join(VRRP_DIRECTORY, 'keepalived.conf')
        try:
            with open(path) as config:
                contents = config.read()
        except (IOError, OSError):
            return []  # No HA

        ret = []

        instances_re = r'vrrp_instance\s+\w+\s+{([\w\W]+)}'
        for instance in finditer(instances_re, contents, MULTILINE):

            network_re = r"notify_master " \
                r"\"/etc/multicloud/vrrp/vrrp_notify.sh (\d+\.\d+\.\d+\.\d+/\d+)"

            network = search(network_re, instance.group(1))
            network = network.group(1)

            vip = r'virtual_ipaddress\s+{\s+(\d+\.\d+\.\d+\.\d+/\d+)\s+dev\s+(\w+)\s+}'

            re = finditer(vip, instance.group(1), MULTILINE)

            for match in re:
                ret.append((network, match.group(2), match.group(1)))

        return ret

    def get_status(self):
        markers = self.get_markers()

        ret = []

        for network, interface, ip in markers:
            addresses = netifaces.ifaddresses(interface)[netifaces.AF_INET]
            ip = ip.split('/')[0]
            addresses = filter(lambda x: x['addr'] == ip, addresses)

            ret.append(
                HAStatus(
                    network=network,
                    master_status=True if len(addresses) == 1 else False))

        return ret

    def __init__(self, logger):
        Multicloud.__init__(self, logger)

        self.names = VRRP_CONTAINER_NAMES
