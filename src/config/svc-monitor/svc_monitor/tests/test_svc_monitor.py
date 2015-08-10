import mock
import unittest
from mock import patch

from svc_monitor.svc_monitor import SvcMonitor
from pysandesh.sandesh_base import Sandesh
from svc_monitor.logger import ServiceMonitorLogger
from svc_monitor.config_db import *
from cfgm_common.vnc_cassandra import VncCassandraClient
from cfgm_common.vnc_kombu import VncKombuClient

class Arguments(object):
    def __init__(self):
        self.disc_server_ip = None
        self.disc_server_port = None
        self.collectors = None
        self.http_server_port = 0
        self.log_local = None
        self.log_category = None
        self.log_level = None
        self.log_file = './svc_monitor/tests/svc_monitor.log'
        self.trace_file = './svc_monitor/tests/svc_monitor.err'
        self.use_syslog = False
        self.cluster_id = None
        self.si_netns_scheduler_driver = \
            'svc_monitor.scheduler.vrouter_scheduler.RandomScheduler'
        self.analytics_server_ip = '127.0.0.1'
        self.analytics_server_port = '8081'
        self.availability_zone = None
        self.auth_protocol = 'http'
        self.auth_host = 'localhost'
        self.auth_port = '8080'
        self.auth_version = 'v2'
        self.admin_user = 'admin'
        self.admin_password = 'password'
        self.region_name = 'region'
        self.auth_insecure = True
        self.syslog_facility = Sandesh._DEFAULT_SYSLOG_FACILITY
        self.logging_conf = ''
        self.logger_class = None
        self.rabbit_server = '127.0.0.1'
        self.rabbit_port = '5672'
        self.rabbit_user = 'guest'
        self.rabbit_password = 'guest'
        self.rabbit_vhost = None
        self.rabbit_ha_mode = False
        self.reset_config = False
        self.cassandra_user = None
        self.cassandra_password = None
        self.cassandra_server_list = None

class SvcMonitorTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def test_svc_monitor_init(self):
        self.args = Arguments()
        VncCassandraClient.__init__ = mock.MagicMock()    
        VncCassandraClient._cf_dict = {'service_instance_table':None, 'pool_table':None}
        VncKombuClient.__init__ = mock.MagicMock(return_value=None)
        ServiceMonitorLogger.__init__ = mock.MagicMock(return_value=None)
        ServiceMonitorLogger.log = mock.MagicMock()
        self._svc_monitor = SvcMonitor(self.args)

        self.vnc_mock = mock.MagicMock()
        def db_list(obj_type):
            return (False, None)
        DBBaseSM._cassandra = mock.MagicMock()
        DBBaseSM._cassandra.list = db_list
        self._svc_monitor.post_init(self.vnc_mock, self.args)
