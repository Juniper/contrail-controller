from mock import Mock
from unittest import TestCase
from datetime import timedelta
import logging
from httpretty import httprettified, register_uri, POST, last_request
from stats.main import Scheduler, Postman, Stats, init_logger


class StatsClientTest(TestCase):
    def setUp(self):
        self.vnc_client = Mock()


class UpdatedSendFreqTest(StatsClientTest):
    def setUp(self):
        super(UpdatedSendFreqTest, self).setUp()
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
        self.assertIsNone(send_freq)


@httprettified
class StatsSendTest(StatsClientTest):

    def test_stats_sending(self):
        self.stats_server = "http://127.0.0.1:22"
        self.postman = Postman(
            stats_server=self.stats_server,
            vnc_client=self.vnc_client,
            logger=init_logger(
                log_level=logging.DEBUG, log_file="tests_stats.txt"))
        self.vnc_client.get_default_project_id.return_value = "test123456"
        self.vnc_client.virtual_machines_list.return_value = {
            "virtual-machines": ["test"]}
        self.vnc_client.virtual_networks_list.return_value = {
            "virtual-networks": ["test", "test2"]}
        self.vnc_client.virtual_routers_list.return_value = {
            "virtual-routers": ["test", "test2", "test3"]}
        self.vnc_client.virtual_machine_interfaces_list.return_value = {
            "virtual-machine-interfaces": ["test", "test2", "test3", "test4"]}
        register_uri(POST, self.stats_server)
        self.postman.send_stats()
        send_request = last_request()
        assert send_request.headers.get('Content-Type') == "application/json"
        assert send_request.method == "POST"
        assert send_request.parsed_body == {
            "tf_id": self.vnc_client.get_default_project_id.return_value,
            "vmachines": 1,
            "vnetworks": 2,
            "vrouters": 3,
            "vm_interfaces": 4
            }
