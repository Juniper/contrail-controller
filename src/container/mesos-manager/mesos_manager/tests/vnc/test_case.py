import sys
import time
import os

sys.path.append("../../config/common/tests")
from test_utils import *
import test_common
sys.path.insert(0, '../../../../build/production/container/mesos-manager')

import tempfile
from vnc_api.vnc_api import *
from cfgm_common import vnc_cgitb
from mesos_manager.mesos_manager import *
from mesos_manager.common import args as mesos_args

class MMTestCase(test_common.TestCase):

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        extra_config = [
            ('DEFAULTS', 'multi_tenancy', 'False'),
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
        ]
        if extra_config_knobs:
            extra_config.append(extra_config_knobs)
        super(MMTestCase, cls).setUpClass(extra_config_knobs=extra_config)
        cls._svc_mon_greenlet = gevent.spawn(test_common.launch_svc_monitor,
                                             cls._cluster_id, cls.__name__,
                                             cls._api_server_ip,
                                             cls._api_server_port)

        cls._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
                                        cls._cluster_id, cls.__name__,
                                        cls._api_server_ip,
                                        cls._api_server_port)

        mesos_config = [
            ('DEFAULTS', 'log_file', 'contrail-mesos-manager.log'),
            ('VNC', 'vnc_endpoint_ip', cls._api_server_ip),
            ('VNC', 'vnc_endpoint_port', cls._api_server_port),
            ('VNC', 'cassandra_server_list', "0.0.0.0:9160"),
            ('MESOS', 'service_subnets', "10.96.0.0/12"),
            ('MESOS', 'app_subnets', "10.32.0.0/12"),
        ]
        cls.event_queue = Queue()
        cls._mm_greenlet = gevent.spawn(test_common.launch_mesos_manager,
                                        cls.__name__, mesos_config, True,
                                        cls.event_queue)
        test_common.wait_for_mesos_manager_up()

    @classmethod
    def tearDownClass(cls):
        test_common.kill_svc_monitor(cls._svc_mon_greenlet)
        test_common.kill_schema_transformer(cls._st_greenlet)
        test_common.kill_mesos_manager(cls._mm_greenlet)
        super(MMTestCase, cls).tearDownClass()

    def _class_str(self):
        return str(self.__class__).strip('<class ').strip('>').strip("'")

    def setUp(self, extra_config_knobs=None):
        super(MMTestCase, self).setUp(extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(MMTestCase, self).tearDown()

    def enqueue_event(self, event):
        self.event_queue.put(event)
        while self.event_queue.empty() is False:
            time.sleep(1)

    def generate_mesos_args(self):
        args_str = ""
        mesos_config = [
            ('DEFAULTS', 'log_file', 'contrail-mesos-manager.log'),
            ('VNC', 'vnc_endpoint_ip', self._api_server_ip),
            ('VNC', 'vnc_endpoint_port', self._api_server_port),
            ('VNC', 'cassandra_server_list', "0.0.0.0:9160"),
            ('MESOS', 'service_subnets', "10.96.0.0/12"),
            ('MESOS', 'app_subnets', "10.32.0.0/12"),
        ]
        vnc_cgitb.enable(format='text')

        with tempfile.NamedTemporaryFile() as conf, tempfile.NamedTemporaryFile() as logconf:
            cfg_parser = test_common.generate_conf_file_contents(mesos_config)
            cfg_parser.write(conf)
            conf.flush()

            cfg_parser = test_common.generate_logconf_file_contents()
            cfg_parser.write(logconf)
            logconf.flush()

            args_str = ["-c", conf.name]
            args = mesos_args.parse_args(args_str)
            return args

    def test_test(self):
        pass
