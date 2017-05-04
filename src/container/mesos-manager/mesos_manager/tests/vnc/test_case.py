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

    def _class_str(self):
        return str(self.__class__).strip('<class ').strip('>').strip("'")

    def setUp(self, extra_config_knobs=None):
        super(MMTestCase, self).setUp(extra_config_knobs=extra_config_knobs)
        self._svc_mon_greenlet = gevent.spawn(test_common.launch_svc_monitor,
            self.id(), self._api_server_ip, self._api_server_port)

        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self.id(), self._api_server_ip, self._api_server_port, extra_config_knobs)

        mesos_config = [
            ('DEFAULTS', 'log_file', 'contrail-mesos-manager.log'),
            ('VNC', 'vnc_endpoint_ip', self._api_server_ip),
            ('VNC', 'vnc_endpoint_port', self._api_server_port),
            ('VNC', 'cassandra_server_list', "0.0.0.0:9160"),
            ('MESOS', 'service_subnets', "10.96.0.0/12"),
            ('MESOS', 'pod_subnets', "10.32.0.0/12"),
        ]
        self.event_queue = Queue()
        self._mm_greenlet = gevent.spawn(test_common.launch_mesos_manager,
            self.id(), mesos_config, True, self.event_queue)

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
            ('MESOS', 'pod_subnets', "10.32.0.0/12"),
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

