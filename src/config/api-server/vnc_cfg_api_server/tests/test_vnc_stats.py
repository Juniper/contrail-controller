#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import datetime
import unittest

import mock

from cfgm_common.uve.vnc_api.ttypes import VncApiLatencyStatsLog
from cfgm_common.vnc_api_stats import log_api_stats
from vnc_cfg_api_server.vnc_db import VncDbClient


class TestVncApiStats(unittest.TestCase):
    def test_vnc_api_stats(self):
        with mock.patch(
                'cfgm_common.vnc_api_stats.VncApiStatistics') as VncApiStatistics:
            api_server = mock.MagicMock()

            api_server.enable_api_stats_log = False
            r = log_api_stats(lambda x, y: 'response1')(api_server, '')
            self.assertFalse(VncApiStatistics.called)
            self.assertEqual('response1', r)

            api_server.enable_api_stats_log = False
            self.assertRaises(
                Exception,
                log_api_stats(Exception)(api_server, ''))
            self.assertFalse(VncApiStatistics.called)

            api_server.enable_api_stats_log = True
            r = log_api_stats(lambda x, y: 'response2')(api_server, 'obj')
            VncApiStatistics.assert_called_once_with(obj_type='obj')
            self.assertEqual('response2', r)

            api_server.enable_api_stats_log = True
            self.assertRaises(
                Exception,
                log_api_stats(Exception)(api_server, ''))
            self.assertTrue(VncApiStatistics.called)


class TestVncLatencyStats(unittest.TestCase):
    def test_latency_stats(self):
        with mock.patch.object(VncApiLatencyStatsLog, 'send') as mocked_send:
            # Let's make easy to create the class.
            VncDbClient.__init__ = lambda self: None

            api_server = mock.MagicMock()
            sandesh = mock.MagicMock()

            vnc_db = VncDbClient()
            vnc_db._api_svr_mgr = api_server
            vnc_db._sandesh = sandesh

            response_time = datetime.datetime.now() - datetime.datetime.now()

            api_server.enable_latency_stats_log = False
            vnc_db.log_db_response_time(mock.MagicMock(), response_time, 'Fake')
            self.assertFalse(mocked_send.called)

            api_server.enable_latency_stats_log = True
            vnc_db.log_db_response_time(mock.MagicMock(), response_time, 'Fake')
            mocked_send.assert_called_once_with(sandesh=sandesh)
