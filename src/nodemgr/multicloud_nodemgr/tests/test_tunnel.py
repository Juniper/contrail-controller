from mock import patch, mock_open
import unittest

from tunnel import IFStats, Tunnel


class TestTunnel(unittest.TestCase):
    data = "enp0s31f6: 6362273303 4547241    0    0    0     0          0     13255 "\
           "100410053  854121    0    0    0     0       0          0"

    def test_creation(self):
        Tunnel(None)

    def test_get_ifaces(self):
        tun = Tunnel(None)

        self.assertIsInstance(tun.get_ifaces(), dict)

    def test_get_if_stats(self):
        tun = Tunnel(None)

        tun.get_ifaces = lambda: {'10.0.0.1': 'enp0s31f6'}

        with patch("__builtin__.open", mock_open(read_data=TestTunnel.data)) as mock_file:
            stats = tun.get_if_stats()
            self.assertEquals(stats, {
                '10.0.0.1': IFStats(byte_rx=6362273303, byte_tx=100410053, packet_rx=4547241, packet_tx=854121)
            })

            mock_file.assert_called_with('/proc/net/dev', 'r')

    def test_parse_if_stats(self):
        tun = Tunnel(None)

        ifaces = {'enp0s31f6': '10.0.0.1'}

        stats = tun.parse_if_stats(ifaces, TestTunnel.data)
        self.assertEquals(stats, {
            '10.0.0.1': IFStats(byte_rx=6362273303, byte_tx=100410053, packet_rx=4547241, packet_tx=854121)
        })
