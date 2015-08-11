import mock
import unittest
from mock import patch

from svc_monitor import svc_monitor
from pysandesh.sandesh_base import Sandesh
from svc_monitor.logger import ServiceMonitorLogger
from cfgm_common.vnc_db import DBBase
from svc_monitor import config_db
from vnc_api.vnc_api import *
import test_common_utils as test_utils
from cfgm_common.vnc_cassandra import VncCassandraClient
from cfgm_common.vnc_kombu import VncKombuClient


si_oper_info = {
    u'oper': u'CREATE', u'uuid': u'fake-instance', u'type': u'service-instance',
    u'obj_dict': {
        u'fq_name': [u'fake-domain', u'fake-project', u'fake-instance'],
        u'parent_uuid': u'fake-domain:fake-project',
        u'service_instance_properties': {
            u'scale_out': {u'max_instances': 2},
            u'interface_list': [
                {u'virtual_network': u''},
                {u'virtual_network': u'fake-domain:fake-project:left_vn'},
                {u'virtual_network': u'fake-domain:fake-project:right_vn'}
            ]
        },
        u'parent_type': u'project'
    }
}

si_del_info = {
    u'oper': u'DELETE', u'uuid': u'fake-instance', u'type': u'service-instance',
    u'obj_dict': {
        u'virtual_machine_back_refs': [{u'to': [u'fake-vm'], u'uuid': u'fake-vm'}],
        u'fq_name': [u'fake-domain', u'fake-project', u'fake-instance'],
        u'uuid': u'fake-instance',
        u'service_instance_properties': {
            u'scale_out': {u'max_instances': 1}, 
            u'interface_list': [
                {u'virtual_network': u''}, 
                {u'virtual_network': u'default-domain:admin:left_vn'},
                {u'virtual_network': u'default-domain:admin:right_vn'}
            ]
        }, 
        u'parent_type': u'project'
    },
    u'display_name': u'fake-instance',
    u'service_template_refs': [{u'to': [u'fake-domain', u'fake-template'], u'uuid': u'fake-template'}],
    u'parent_uuid': u'fake-project'
}

class SvcMonitorTest(unittest.TestCase):
    def setUp(self):
        self.args = svc_monitor.parse_args('')
        ServiceMonitorLogger.__init__ = mock.MagicMock(return_value=None)
        ServiceMonitorLogger.log = mock.MagicMock()
        ServiceMonitorLogger.log_info = mock.MagicMock()
        ServiceMonitorLogger.uve_svc_instance = mock.MagicMock()
        VncCassandraClient.__init__ = mock.MagicMock()    
        VncCassandraClient._cf_dict = {'service_instance_table':None, 'pool_table':None}
        VncKombuClient.__init__ = mock.MagicMock(return_value=None)
        self.vnc_mock = mock.MagicMock()

        self._svc_monitor = svc_monitor.SvcMonitor(self.args)

        self.add_domain("default-domain", 'default-domain')
        self.vnc_mock.service_template_create = test_utils.st_create
        config_db.DBBaseSM._cassandra = mock.MagicMock()
        config_db.DBBaseSM._cassandra.list = self.db_list
        config_db.DBBaseSM._cassandra.read = self.db_read

        self._svc_monitor.post_init(self.vnc_mock, self.args)

    def tearDown(self):
        config_db.ServiceTemplateSM.reset()
        config_db.ServiceInstanceSM.reset()
        config_db.VirtualNetworkSM.reset()
        config_db.SecurityGroupSM.reset()
        config_db.VirtualMachineSM.reset()
        del config_db.DBBaseSM._cassandra
        del self.vnc_mock

    def obj_to_dict(self, obj):
        def to_json(obj):
            if hasattr(obj, 'serialize_to_json'):
                return obj.serialize_to_json(obj.get_pending_updates())
            else:
                return dict((k, v) for k, v in obj.__dict__.iteritems())

        return json.loads(json.dumps(obj, default=to_json))

    def add_domain(self, name, uuid):
        dom = Domain(name)
        dom_dict = self.obj_to_dict(dom)
        config_db.DomainSM._cassandra.read = mock.Mock(return_value=(True, [dom_dict]))
        config_db.DomainSM.locate(uuid)

    def add_si(self, name, uuid, st):
        si = ServiceInstance(name=name)
        si.set_service_template(st)
        si_dict = self.obj_to_dict(si)
        si_dict['uuid'] = uuid
        si_obj = ServiceInstance.from_dict(**si_dict)
        config_db.ServiceInstanceSM._cassandra.read = mock.Mock(return_value=(True, [si_dict]))
        config_db.ServiceInstanceSM.locate(uuid)
        return si_obj

    def add_st(self, name, uuid):
        st = ServiceTemplate(name=name)
        st_dict = self.obj_to_dict(st)
        st_dict['uuid'] = uuid
        st_obj = ServiceTemplate.from_dict(**st_dict)
        config_db.ServiceTemplateSM._cassandra.read = mock.Mock(return_value=(True, [st_dict]))
        config_db.ServiceTemplateSM.locate(uuid)
        return st_obj

    def add_vm(self, name, uuid, si, virt_type=None):
        vm = VirtualMachine(name=name)
        vm.set_service_instance(si)
        if virt_type:
            name = si.name + '__' + '1'
            instance_name = "__".join(si.fq_name[:-1] + [name])
            vm.set_display_name(instance_name + '__' + virt_type)
        vm_dict = self.obj_to_dict(vm)
        vm_dict['uuid'] = uuid
        vm_obj = VirtualMachine.from_dict(**vm_dict)
        config_db.VirtualMachineSM._cassandra.read = mock.Mock(return_value=(True, [vm_dict]))
        config_db.VirtualMachineSM.locate(uuid)
        return vm_obj

    def si_db_read(self, obj_type, uuids):
        if obj_type != 'service_instance':
            return (False, None)
        obj = {}
        obj['uuid'] = self.si.uuid
        obj['fq_name'] = self.si.fq_name
        obj['service_instance_properties'] = self.si.params
        obj['service_template_refs'] = \
            [{'to': [u'default-domain', u'fake-template'], 
              'uuid': 'fake-template'}]
        return (True, [obj])

    def db_read(self, obj_type, uuids):
        if obj_type == 'domain':
            obj = {}
            obj['fq_name'] = 'default-domain'
            obj['uuid'] = uuids[0]
            return (True, [obj])
        return (False, None)

    def db_list(self, obj_type):
        if obj_type == 'domain':
            fq_name = 'default-domain'
            return (True, [([fq_name], obj_type)])
        return (False, None)

    def test_svc_monitor_init(self):
        ServiceMonitorLogger.log_info.assert_any_call(test_utils.AnyStringWith('template created with uuid'))
        self.assertTrue(self._svc_monitor._db_resync_done)

    def test_svc_monitor_upgrade(self):
        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'servers' and oper == 'get':
                nova_vm = test_utils.FakeNovaServer('fake-vm',
                    'default-domain__fake-project__fake-instance__1')
                return nova_vm
            else:
                return mock.MagicMock()
        self._svc_monitor._nova_client = mock.MagicMock()
        self._svc_monitor._nova_client.oper = nova_oper

        st_obj = self.add_st('fake-template', 'fake-template')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'virtual-machine'
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm('fake-vm', 'fake-vm', si_obj)
        self._svc_monitor.upgrade()
        self.vnc_mock.virtual_machine_update.assert_any_call(test_utils.VMObjMatcher(1))

    def test_svc_monitor_vm_service_create(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'virtual-machine'

        self._svc_monitor.vm_manager = mock.MagicMock()
        self._svc_monitor._vnc_subscribe_callback(si_oper_info)
        self._svc_monitor.vm_manager.create_service.assert_called_with(st, si)
        self.assertTrue(si.launch_count==1)
        match_str = "SI %s creation success" % (':').join(si.fq_name)
        ServiceMonitorLogger.log_info.assert_any_call(match_str)

    def test_svc_monitor_vm_service_delete(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm("fake-vm", 'fake-vm', si_obj, 'virtual-machine')
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.virtual_machines.add('fake-vm')
        vm = config_db.VirtualMachineSM.get('fake-vm')

        self._svc_monitor.vm_manager = mock.MagicMock()
        self._svc_monitor._vnc_subscribe_callback(si_del_info)
        self._svc_monitor.vm_manager.delete_service.assert_called_with(vm)
        ServiceMonitorLogger.log_info.assert_any_call(test_utils.AnyStringWith('deletion succeed'))

    def test_svc_monitor_snat_service_create(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'network-namespace'

        self._svc_monitor.netns_manager = mock.MagicMock()
        self._svc_monitor._vnc_subscribe_callback(si_oper_info)
        self._svc_monitor.netns_manager.create_service.assert_called_with(st, si)
        self.assertTrue(si.launch_count==1)
        match_str = "SI %s creation success" % (':').join(si.fq_name)
        ServiceMonitorLogger.log_info.assert_any_call(match_str)

    def test_svc_monitor_snat_service_delete(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm("fake-vm", 'fake-vm', si_obj, 'network-namespace')
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.virtual_machines.add('fake-vm')
        vm = config_db.VirtualMachineSM.get('fake-vm')

        self._svc_monitor.netns_manager = mock.MagicMock()
        self._svc_monitor._vnc_subscribe_callback(si_del_info)
        self._svc_monitor.netns_manager.delete_service.assert_called_with(vm)
        ServiceMonitorLogger.log_info.assert_any_call(test_utils.AnyStringWith('deletion succeed'))

    def test_svc_monitor_vrouter_service_create(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'vrouter-instance'

        self._svc_monitor.vrouter_manager = mock.MagicMock()
        self._svc_monitor._vnc_subscribe_callback(si_oper_info)
        self._svc_monitor.vrouter_manager.create_service.assert_called_with(st, si)
        self.assertTrue(si.launch_count==1)
        match_str = "SI %s creation success" % (':').join(si.fq_name)
        ServiceMonitorLogger.log_info.assert_any_call(match_str)

    def test_svc_monitor_vrouter_service_delete(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm("fake-vm", 'fake-vm', si_obj, 'vrouter-instance')
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.virtual_machines.add('fake-vm')
        vm = config_db.VirtualMachineSM.get('fake-vm')

        self._svc_monitor.vrouter_manager = mock.MagicMock()
        self._svc_monitor._vnc_subscribe_callback(si_del_info)
        self._svc_monitor.vrouter_manager.delete_service.assert_called_with(vm)
        ServiceMonitorLogger.log_info.assert_any_call(test_utils.AnyStringWith('deletion succeed'))
