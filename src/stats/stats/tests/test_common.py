import mock
import unittest
from datetime import timedelta

from stats.main import Scheduler


class StatsTest(unittest.TestCase):
    def setUp(self):
        self.vnc_client = mock.Mock()
        self.vnc_client.tags_list.return_value = {"tags": []}
        self.scheduler = Scheduler(self.vnc_client)

    def test_get_updated_send_freq_monthly(self):
        self.vnc_client.tags_list.return_value = {"tags": [
            {"fq_name": ["label=stats_monthly"]},
            {"fq_name": ["label=stats_weekly"]},
            {"fq_name": ["label=stats_daily"]},
            {"fq_name": ["test"]}
            ]}
        send_freq = self.scheduler._get_updated_send_freq()
        self.assertEqual(send_freq, timedelta(days=30))

    def test_get_updated_send_freq_daily(self):
        self.vnc_client.tags_list.return_value = {"tags": [
            {"fq_name": ["label=stats_daily"]},
            {"fq_name": ["test"]}
            ]}
        send_freq = self.scheduler._get_updated_send_freq()
        self.assertEqual(send_freq, timedelta(days=1))

    def test_get_updated_send_freq_swithched_off(self):
        self.vnc_client.tags_list.return_value = {"tags":
                                                  [{"fq_name": ["test"]}]}
        send_freq = self.scheduler._get_updated_send_freq()
        self.assertEqual(send_freq, None)
