import contextlib
import logging
import mock
import unittest

from mock import patch

from svc_monitor.svc_monitor import SvcMonitor
from svc_monitor.db import ServiceMonitorDB
from pysandesh.sandesh_base import Sandesh

from vnc_api.vnc_api import IdPermsType
from vnc_api.vnc_api import ServiceInstance, ServiceInstanceType
from vnc_api.vnc_api import ServiceScaleOutType
from vnc_api.vnc_api import ServiceTemplate, ServiceTemplateType

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
        self.trace_file = '/var/log/contrail/svc_monitor.err'
        self.use_syslog = False
        self.syslog_facility = Sandesh._DEFAULT_SYSLOG_FACILITY
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

class SvcMonitorInitTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch.object(ServiceMonitorDB, '_cassandra_init')
    def test_vm_instance(self, cassandra_init_mock):
        logging.debug("init")
        self._api_client = mock.Mock()
        arguments = Arguments()
        with patch.object(logging.handlers, 'RotatingFileHandler'):
            self._svc_monitor = SvcMonitor(arguments)
            self._svc_monitor.post_init(self._api_client, arguments)
            self._svc_monitor.db._svc_si_cf = {}
            self._svc_monitor.db._svc_vm_cf = mock.Mock()
            self._svc_monitor._novaclient_get = mock.Mock()
            identities = {
                'service-template': 'default-domain:test:template1',
                'service-instance': 'default-domain:test:service1'
            }
            tmpl_attr = ServiceTemplateType()
            tmpl_attr.service_mode = 'in-network-nat'
            tmpl_attr.service_type = 'firewall'
            tmpl_attr.image_name = 'test-template'
            tmpl_attr.service_virtualization_type = 'virtual-machine'
            template = ServiceTemplate(service_template_properties=tmpl_attr)
            template.uuid = 'aaa'
            svc_attr = ServiceInstanceType()
            svc_attr.left_virtual_network = 'default-project:demo:test'
            svc_attr.right_virtual_network = 'default-project:admin:public'
            svc_attr.scale_out = ServiceScaleOutType()
            service = ServiceInstance('test-instance',
                                      service_instance_properties=svc_attr)
            service.uuid = 'bbb'
            self._api_client.service_template_read.return_value = template
            self._api_client.service_instance_read.return_value = service

            with contextlib.nested(
                patch.object(self._svc_monitor.vm_manager,
                             '_create_svc_vm'),
                patch.object(self._svc_monitor.db,
                             'service_instance_insert')) as (create_svc_vm, svc_insert):

                class Vm(object):
                    @property
                    def id(self):
                        return 'ccc'

                create_svc_vm.return_value = Vm()
                self._svc_monitor.\
                    _addmsg_service_instance_service_template(identities)

                svc_insert.assert_called_with(
                    'default-domain:default-project:test-instance',
                    {'vm0-state': 'pending', 'vm0-preference': '0',
                     'vm0-uuid': 'ccc', 'vm0-vrouter': 'None',
                     'vm0-name': 'default-domain__default-project__bbb__1'})

