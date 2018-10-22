from collections import namedtuple
from re import finditer, MULTILINE

from multicloud import Multicloud

IFStats = namedtuple("IFStats", ["byte_rx", "byte_tx", "packet_rx", "packet_tx"])
TunnelStatus = namedtuple("TunnelStatus", ["peer", "since", "is_up"])


class Tunnel(Multicloud):
    def get_ifaces(self):
        return {}

    def read_if_stats(self):
        with open('/proc/net/dev', 'r') as f:
            content = f.read()

        return content

    def parse_if_stats(self, ifaces, stats):
        # For example
        # enp0s31f6: 6362273303 4547241    0    0    0     0          0     \
        # 13255 100410053  854121    0    0    0     0       0          0
        re = r'(.+):\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+'\
             r'(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)'
        re = finditer(re, stats, MULTILINE)

        stats = {}

        for match in re:
            ifname = match.group(1)
            if ifname in ifaces.keys():
                stats[ifaces[ifname]] = IFStats(
                    byte_rx=int(match.group(2)),
                    byte_tx=int(match.group(10)),
                    packet_rx=int(match.group(3)),
                    packet_tx=int(match.group(11))
                )

        return stats

    def get_if_stats(self):
        ifaces = self.get_ifaces()
        ifaces = {name: ip for ip, name in ifaces.iteritems()}

        stats = self.read_if_stats()

        return self.parse_if_stats(ifaces, stats)
