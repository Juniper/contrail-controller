import mock
import unittest
import uuid
import sys
import subprocess
import pdb

from stats_daemon.storage_nodemgr import EventManager
from pysandesh.sandesh_base import *

class EventManagerTest(unittest.TestCase):
    def setUp(self):
        self._api = EventManager("storage-compute")

    def mocked_pool_subprocess_call(self, arg1, stderr, shell):
        if arg1 == '/usr/bin/rados df':
            return "pool name       category                 KB      " + \
                   "objects       clones     degraded      unfound   " + \
                   "        rd        rd KB           wr        wr KB\n" + \
                   "images          -                  431045012     " + \
                   "   24144            0            0           0   " + \
                   "         0            0        61764    220548992\n" + \
                   "volumes         -                  431072528      " + \
                   "  24145            0            0           0     " + \
                   "       0            0        60484    215975742"

    def test_create_and_send_pool_stats(self):
        subprocess.check_output = self.mocked_pool_subprocess_call
        SandeshUVE.send = mock.MagicMock(return_value=1)
        self._api.create_and_send_pool_stats()
        SandeshUVE.send.assert_called_with()


    def mocked_osd_subprocess_call(self, arg1, stderr, shell):
        if arg1.find('ls /var/lib/ceph/osd') != -1:
            return "ceph-0\nceph-1"
        if arg1.find('cat /var/lib/ceph/osd') != -1:
            return "ok\n"
        if arg1.find('ceph osd dump | grep') != -1:
            return "abcdefghuuiduuid\n"
        if arg1.find('subop_r_out_bytes') != -1:
            return   "subop_r_out_bytes : 100000\n" + \
                     "op_r_out_bytes : 1200000\n" + \
                     "subop_w_in_bytes : 10000000\n" + \
                     "op_w_in_bytes : 10000000\n" + \
                     "subop_r : 1000000\n" + \
                     "op_r : 10000000\n" + \
                     "subop_w : 1000000\n" + \
                     "op_w : 10000000\n"
        if arg1.find('subop_r_latency') != -1:
            return   "op_w_latency : { avgcount: 259289,\n" + \
                     "sum: 101967.951216000}, \n" + \
                     "op_r_latency: { avgcount: 659289,\n" + \
                     "sum: 151967.15}, \n" + \
                     "subop_w_latency: { avgcount: 9289,\n" + \
                     "sum: 1767.95}, \n" + \
                     "subop_r_latency: { avgcount: 659,\n" + \
                     "sum: 1544.15},"


    def test_create_and_send_osd_stats(self):
        subprocess.check_output = self.mocked_osd_subprocess_call
        SandeshUVE.send = mock.MagicMock(return_value=1)
        self._api.create_and_send_osd_stats()
        SandeshUVE.send.assert_called_with()

    def mocked_diskstats_subprocess_call(self, arg1, stderr, shell):
        if arg1.find('iostat') != -1:
            return  "sda               4         432        94 " + \
                    "    785643   17196348\n" + \
                    "sdb              13         186      5108 " + \
                    "    331088  929245099\n" + \
                    "sdc              94      108048       383 " + \
                    "196540614   69852410\n" + \
                    "sdd              11         159      4413 " + \
                    "   283430  802800232\n"
        if arg1.find('cat /sys/block') != -1:
            return "10 20 30 40 50 60 70 80 90 100\n" + \
                   "11 21 31 41 51 61 71 81 91 101\n" + \
                   "12 22 32 42 52 62 72 82 92 102\n" + \
                   "13 23 33 43 53 63 73 83 93 103\n"

        if arg1.find('disk list') != -1:
            return "esbu-mflab-lnx-c02][INFO  ] /dev/sda :\n" + \
                   "[esbu-mflab-lnx-c02][INFO  ]  /dev/sda1 other, ext4, mounted on /\n" + \
                   "[esbu-mflab-lnx-c02][INFO  ]  /dev/sdb1 ceph data, active, cluster ceph, osd.0, journal /dev/sdb2\n" + \
                   "[esbu-mflab-lnx-c02][INFO  ]  /dev/sdd1 ceph data, active, cluster ceph, osd.1, journal /dev/sdd2\n" + \
                   "[esbu-mflab-lnx-c02][INFO  ]  /dev/sdd2 ceph journal, for /dev/sdd1\n"
        if arg1.find('df -h') != -1:
            return "Filesystem      Size  Used Avail Use% Mounted on\n" + \
                   "/dev/sda1       215G  2.7G  201G   2% /\n" + \
                   "/dev/sdb1       465G  442G   24G  96% /var/lib/ceph/osd/ceph-0\n" + \
                   "/dev/sdd1       465G  382G   84G  83% /var/lib/ceph/osd/ceph-1\n"

        if arg1.find('by-id') != -1:
            return "lrwxrwxrwx 1 root root  9 Jul 23 14:55 ata-INTEL_SSDSC2BA200G3_BTTV33450B3K200GGN -> ../../sdc\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-INTEL_SSDSC2BA200G3_BTTV33450B3K200GGN-part1 -> ../../sdc1\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-INTEL_SSDSC2BA200G3_BTTV33450B3K200GGN-part2 -> ../../sdc2\n" + \
                   "lrwxrwxrwx 1 root root  9 Jul 23 14:55 ata-WDC_WD2503ABYX-01WERA1_WD-WMAYP3911052 -> ../../sda\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-WDC_WD2503ABYX-01WERA1_WD-WMAYP3911052-part1 -> ../../sda1\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-WDC_WD2503ABYX-01WERA1_WD-WMAYP3911052-part2 -> ../../sda2\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-WDC_WD2503ABYX-01WERA1_WD-WMAYP3911052-part5 -> ../../sda5\n" + \
                   "lrwxrwxrwx 1 root root  9 Jul 23 14:55 ata-WDC_WD5000HHTZ-04N21V0_WD-WXA1E63CYSX7 -> ../../sdb\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-WDC_WD5000HHTZ-04N21V0_WD-WXA1E63CYSX7-part1 -> ../../sdb1\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-WDC_WD5000HHTZ-04N21V0_WD-WXA1E63CYSX7-part2 -> ../../sdb2\n" + \
                   "lrwxrwxrwx 1 root root  9 Jul 23 14:55 ata-WDC_WD5000HHTZ-04N21V0_WD-WXH1E33XJHM1 -> ../../sdd\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-WDC_WD5000HHTZ-04N21V0_WD-WXH1E33XJHM1-part1 -> ../../sdd1\n" + \
                   "lrwxrwxrwx 1 root root 10 Jul 20 22:00 ata-WDC_WD5000HHTZ-04N21V0_WD-WXH1E33XJHM1-part2 -> ../../sdd2\n"


    def test_create_and_send_disk_stats(self):
        subprocess.check_output = self.mocked_diskstats_subprocess_call
        SandeshUVE.send = mock.MagicMock(return_value=1)
        self._api.create_and_send_disk_stats()
        SandeshUVE.send.assert_called_with()
