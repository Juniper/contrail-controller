import mock
import unittest
import uuid
import sys
import subprocess

from stats_daemon.storage_nodemgr import EventManager
from pysandesh.sandesh_base import *

class EventManagerTest(unittest.TestCase):
    def setUp(self):
        self._api = EventManager("storage-compute")

    def mocked_subprocess_call(self, arg1, stderr, shell):
        return "10 20 30 40 50 60 70 80 90 100 110\n" + \
               "11 21 31 41 51 61 71 81 91 101 111"

    def test_create_and_send_pool_stats_(self):
        subprocess.check_output = self.mocked_subprocess_call
        SandeshUVE.send = mock.MagicMock(return_value=1)
        self._api.create_and_send_pool_stats()
        SandeshUVE.send.assert_called_with()


    def test_create_and_send_osd_stats_(self):
        subprocess.check_output = self.mocked_subprocess_call
        SandeshUVE.send = mock.MagicMock(return_value=1)
        self._api.create_and_send_osd_stats()
        SandeshUVE.send.assert_called_with()

    def mocked_diskstats_subprocess_call(self, arg1, stderr, shell):
        return "sda 20 30 40 50 60 70 80 90 100 110\n" + \
               "sdb 21 31 41 51 61 71 81 91 101 111"


    def test_create_and_send_disk_stats_(self):
        subprocess.check_output = self.mocked_diskstats_subprocess_call
        SandeshUVE.send = mock.MagicMock(return_value=1)
        self._api.create_and_send_disk_stats()
        SandeshUVE.send.assert_called_with()
