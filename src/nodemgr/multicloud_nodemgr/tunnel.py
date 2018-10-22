from abc import ABCMeta, abstractmethod
from collections import namedtuple
from re import finditer, MULTILINE

from multicloud import Multicloud

InterfaceStats = namedtuple("InterfaceStats", ["byte_rx", "byte_tx", "packet_rx", "packet_tx"])
TunnelStatus = namedtuple("TunnelStatus", ["peer", "since", "is_up"])


class Tunnel(Multicloud):
    __metaclass__ = ABCMeta

    @abstractmethod
    def get_interfaces(self):
        raise NotImplementedError

    def read_interface_stats(self):
        with open('/proc/net/dev', 'r') as f:
            content = f.read()

        return content

    def parse_interface_stats(self, interfaces, stats):
        # For example
        # enp0s31f6: 6362273303 4547241    0    0    0     0          0     \
        # 13255 100410053  854121    0    0    0     0       0          0
        re = r'(.+):\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+'\
             r'(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)'
        re = finditer(re, stats, MULTILINE)

        stats = {}

        for match in re:
            interface_name = match.group(1)
            if interface_name in interfaces.keys():
                stats[interfaces[interface_name]] = InterfaceStats(
                    byte_rx=int(match.group(2)),
                    byte_tx=int(match.group(10)),
                    packet_rx=int(match.group(3)),
                    packet_tx=int(match.group(11))
                )

        return stats

    def get_interface_stats(self):
        interfaces = self.get_interfaces()
        interfaces = {name: ip for ip, name in interfaces.iteritems()}

        stats = self.read_interface_stats()

        return self.parse_interface_stats(interfaces, stats)
