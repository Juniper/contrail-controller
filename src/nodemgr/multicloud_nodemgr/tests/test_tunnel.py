from mock import patch, mock_open
import unittest

from tunnel import InterfaceStats, Tunnel


class MockTunnel(Tunnel):
    def get_interfaces(self):
        return {'10.0.0.1': 'enp0s31f6'}


class TestTunnel(unittest.TestCase):
    data = "enp0s31f6: 6362273303 4547241    0    0    0     0          0     13255 "\
           "100410053  854121    0    0    0     0       0          0"

    def test_creation(self):
        MockTunnel(None)

    def test_get_interfaces(self):
        tun = MockTunnel(None)

        self.assertIsInstance(tun.get_interfaces(), dict)

    def test_get_interface_stats(self):
        tun = MockTunnel(None)

        with patch("__builtin__.open", mock_open(read_data=TestTunnel.data)) as mock_file:
            stats = tun.get_interface_stats()
            self.assertEquals(stats, {
                '10.0.0.1': InterfaceStats(byte_rx=6362273303, byte_tx=100410053, packet_rx=4547241, packet_tx=854121)
            })

            mock_file.assert_called_with('/proc/net/dev', 'r')

    def test_parse_interface_stats(self):
        tun = MockTunnel(None)

        interfaces = tun.get_interfaces()
        interfaces = {v: k for (k, v) in interfaces.iteritems()}

        stats = tun.parse_interface_stats(interfaces, TestTunnel.data)
        self.assertEquals(stats, {
            '10.0.0.1': InterfaceStats(byte_rx=6362273303, byte_tx=100410053, packet_rx=4547241, packet_tx=854121)
        })
