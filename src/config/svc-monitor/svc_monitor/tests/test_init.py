import logging
import mock
import unittest

from mock import patch

from svc_monitor.svc_monitor import SvcMonitor
from pysandesh.sandesh_base import Sandesh

class Arguments(object):
    def __init__(self):
        self.disc_server_ip = None
        self.disc_server_port = None
        self.collectors = None
        self.http_server_port = 0
        self.log_local = None
        self.log_category = None
        self.log_level = None
        self.log_file = '/var/log/contrail/svc_monitor.log'
        self.use_syslog = False
        self.syslog_facility = Sandesh._DEFAULT_SYSLOG_FACILITY

class SvcMonitorInitTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('pysandesh.sandesh_base.Sandesh')
    @patch.object(SvcMonitor, '_cassandra_init')
    def test_init_monitor(self, sandesh_mock, cassandra_init_mock):
        logging.debug("init")
        self._api_client = mock.Mock()
        arguments = Arguments()
        with patch.object(logging.handlers, 'RotatingFileHandler'):
            self._svc_monitor = SvcMonitor(self._api_client, arguments)

