"""Main module for testing of statistics service."""
import logging
from datetime import timedelta
from os import remove
from unittest import TestCase

from httpretty import POST, httprettified, last_request, register_uri

from mock import Mock

from stats.main import Postman, Scheduler, init_logger


class StatsClientTest(TestCase):
    """The base class for statistics sending."""

    def setUp(self):
        """Set up for all tests."""
        self.vnc_client = Mock()


class UpdatedSendFreqTest(StatsClientTest):
    """Tests to check config parsing to get right sending frequency setup."""

    STATUS_FILE = "status.json"

    def setUp(self):
        """Set up for testing send frequency configuration."""
        super(UpdatedSendFreqTest, self).setUp()
        self.vnc_client.tags_list.return_value = {"tags": []}
        self.scheduler = Scheduler(self.vnc_client,
                                   state=UpdatedSendFreqTest.STATUS_FILE)

    def tearDown(self):
        """Cleanup after sending frequency testing."""
        remove(UpdatedSendFreqTest.STATUS_FILE)

    def test_get_updated_send_freq_monthly(self):
        """Test sending frequency setting which is equal one month."""
        self.vnc_client.tags_list.return_value = {"tags": [
            {"fq_name": ["label=stats_monthly"]},
            {"fq_name": ["label=stats_weekly"]},
            {"fq_name": ["label=stats_daily"]},
            {"fq_name": ["test"]}
            ]}
        send_freq = self.scheduler._get_updated_send_freq()
        self.assertEqual(send_freq, timedelta(days=30))

    def test_get_updated_send_freq_daily(self):
        """Test sending frequency setting which is equal one day."""
        self.vnc_client.tags_list.return_value = {"tags": [
            {"fq_name": ["label=stats_daily"]},
            {"fq_name": ["test"]}
            ]}
        send_freq = self.scheduler._get_updated_send_freq()
        self.assertEqual(send_freq, timedelta(days=1))

    def test_get_updated_send_freq_swithched_off(self):
        """Test sending frequency setting if it is switched off."""
        self.vnc_client.tags_list.return_value = {"tags":
                                                  [{"fq_name": ["test"]}]}
        send_freq = self.scheduler._get_updated_send_freq()
        self.assertIsNone(send_freq)


@httprettified
class StatsSendTest(StatsClientTest):
    """The class to test sending statistics to server."""

    def test_stats_sending(self):
        """Test sending statistics to server."""
        self.stats_server = "http://127.0.0.1:22/"
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
