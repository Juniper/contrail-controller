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


si_add_info = {
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

vn_add_info = {
    u'oper': u'CREATE', u'uuid': u'left-vn', u'type': u'virtual-network',
    u'obj_dict': {
        u'fq_name': [u'fake-domain', u'fake-project', u'left-vn'],
        u'parent_uuid': u'fake-domain:fake-project',
        u'parent_type': u'project'
    }
}

vmi_add_info = {
    u'oper': u'CREATE', u'uuid': u'left-vmi', u'type': u'virtual-machine-interface',
    u'obj_dict': {
        u'fq_name': [u'fake-domain', u'fake-project', u'fake-domain__fake-project__fake-instance__1__left__0'],
        u'parent_uuid': u'fake-domain:fake-project',
        u'parent_type': u'project'
    }
}

vmi_del_info = {
    u'oper': u'DELETE', u'uuid': u'left-vmi', u'type': u'virtual-machine-interface',
    u'obj_dict': {
        u'fq_name': [u'fake-domain', u'fake-project', u'left-vmi'],
        u'parent_uuid': u'fake-domain:fake-project',
        u'parent_type': u'project'
    }
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
        config_db.VirtualMachineInterfaceSM.reset()
        config_db.ProjectSM.reset()
        config_db.InterfaceRouteTableSM.reset()
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

    def add_project(self, name, uuid):
        project = Project(name=name)
        proj_dict = self.obj_to_dict(project)
        proj_dict['uuid'] = 'project'
        proj_obj = Project.from_dict(**proj_dict)
        config_db.ProjectSM._cassandra.read = mock.Mock(return_value=(True, [proj_dict]))
        config_db.ProjectSM.locate(uuid)
        return proj_obj

    def add_irt(self, name, uuid):
        irt = InterfaceRouteTable(name=name)
        irt_dict = self.obj_to_dict(irt)
        irt_dict['uuid'] = 'irt'
        irt_obj = InterfaceRouteTable.from_dict(**irt_dict)
        config_db.InterfaceRouteTableSM._cassandra.read = mock.Mock(return_value=(True, [irt_dict]))
        config_db.InterfaceRouteTableSM.locate(uuid)
        return irt_obj

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

    def add_vm(self, name, uuid, si=None, virt_type=None):
        vm = VirtualMachine(name=name)
        if si:
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

    def add_vn(self, name, uuid, parent_obj):
        network = VirtualNetwork(name=name, parent_obj=parent_obj)
        net_dict = self.obj_to_dict(network)
        net_dict['parent_uuid'] = parent_obj.uuid
        net_dict['uuid'] = uuid
        net_obj = VirtualNetwork.from_dict(**net_dict)
        config_db.VirtualNetworkSM._cassandra.read = mock.Mock(return_value=(True, [net_dict]))
        config_db.VirtualNetworkSM.locate(uuid)
        return net_obj

    def add_vmi(self, name, uuid, parent_obj, net_obj, vm_obj=None, irt_obj=None):
        vmi = VirtualMachineInterface(name=name, parent_obj=parent_obj)
        vmi.set_virtual_network(net_obj)
        if vm_obj:
            vmi.set_virtual_machine(vm_obj)

        if irt_obj:
            vmi.add_interface_route_table(irt_obj)
            vmi._pending_field_updates.add('interface_route_table_refs')
        vmi_dict = self.obj_to_dict(vmi)
        vmi_dict['parent_uuid'] = parent_obj.uuid
        vmi_dict['uuid'] = uuid
        vmi_obj = VirtualMachineInterface.from_dict(**vmi_dict)
        config_db.VirtualMachineInterfaceSM._cassandra.read = mock.Mock(return_value=(True, [vmi_dict]))
        config_db.VirtualMachineInterfaceSM.locate(uuid)
        return vmi_obj

    def cassandra_vn_read(self, obj_type, uuids):
        obj = {}
        obj['uuid'] = uuids[0]
        obj['fq_name'] = ['fake-domain', 'fake-project', uuids[0]]
        return True, [obj]

    def cassandra_vmi_read(self, obj_type, uuids):
        obj = {}
        obj['uuid'] = 'left-vmi'
        obj['fq_name'] = ['fake-domain', 'fake-project',
                          'fake-domain__fake-project__fake-instance__1__left__0']
        return True, [obj]

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

    def test_svc_monitor_cgitb(self):
        self._svc_monitor._vnc_subscribe_callback(si_add_info)
        self.assertTrue(ServiceMonitorLogger.log.called)

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
        self._svc_monitor._vnc_subscribe_callback(si_add_info)
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

    def test_svc_monitor_vm_delayed_vn_add(self):
        st = test_utils.create_test_st(name='fake-template',
            virt_type='virtual-machine', intf_list=[['left', True]])
        si = test_utils.create_test_si(name='fake-instance', count=1,
            intf_list=['left-vn'])
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.service_template = 'fake-template'
        st = config_db.ServiceTemplateSM.get('fake-template')

        config_db.VirtualNetworkSM._cassandra.read = self.cassandra_vn_read
        self._svc_monitor.vm_manager = mock.MagicMock()
        self._svc_monitor._vnc_subscribe_callback(vn_add_info)
        self._svc_monitor.vm_manager.create_service.assert_called_with(st, si)
        self.assertTrue(si.launch_count==1)
        match_str = "SI %s creation success" % (':').join(si.fq_name)
        ServiceMonitorLogger.log_info.assert_any_call(match_str)

    def test_svc_monitor_vmi_add(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si = test_utils.create_test_si(name='fake-instance', count=1,
            intf_list=['left-vn'])
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.service_template = 'fake-template'
        vm_obj = self.add_vm("fake-vm", 'fake-vm', None, 'virtual-machine')
        project = self.add_project('fake-project', 'fake-project')
        net_obj = self.add_vn('left-vn', 'left-vn', project)
        vmi_obj = self.add_vmi('fake-domain__fake-project__fake-instance__1__left__0', 
                               'left-vmi', project, net_obj, vm_obj)
        vmi = config_db.VirtualMachineInterfaceSM.get('left-vmi')
        vmi.if_type = 'left'

        config_db.VirtualMachineInterfaceSM._cassandra.read = self.cassandra_vmi_read
        self._svc_monitor._vnc_subscribe_callback(vmi_add_info)
        ServiceMonitorLogger.log_info.assert_any_call(test_utils.AnyStringWith('updated SI'))

    def test_svc_monitor_vmi_del(self):
        project = self.add_project('fake-project', 'fake-project')
        net_obj = self.add_vn('left-vn', 'left-vn', project)
        vmi_obj = self.add_vmi('left-vmi', 'left-vmi', project, net_obj)
        irt_obj = self.add_irt('fake-irt', 'fake-irt')
        vmi = config_db.VirtualMachineInterfaceSM.get('left-vmi')
        vmi.interface_route_table = 'fake-irt'

        config_db.VirtualMachineInterfaceSM._cassandra.read = self.cassandra_vmi_read
        self._svc_monitor._vnc_subscribe_callback(vmi_del_info)
        self.vnc_mock.interface_route_table_delete.assert_called_with(id='fake-irt')

    def test_svc_monitor_snat_service_create(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'network-namespace'

        self._svc_monitor.netns_manager = mock.MagicMock()
        self._svc_monitor._vnc_subscribe_callback(si_add_info)
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
        self._svc_monitor._vnc_subscribe_callback(si_add_info)
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

    def test_svc_monitor_timer_delete_vms(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm("fake-vm", 'fake-vm', si_obj, 'virtual-machine')
        vm = config_db.VirtualMachineSM.get('fake-vm')
        config_db.ServiceInstanceSM.delete('fake-instance')
        vm.service_instance = 'non-existent-instance'

        svc_monitor.timer_callback(self._svc_monitor)
        ServiceMonitorLogger.log_info.assert_any_call(test_utils.AnyStringWith('Deleting VM'))

    def test_svc_monitor_timer_check_si_vm(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        st = config_db.ServiceTemplateSM.get('fake-template')
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.launch_count = 1
        self._svc_monitor.vm_manager = mock.MagicMock()
        svc_monitor.timer_callback(self._svc_monitor)
        self._svc_monitor.vm_manager.check_service.assert_called_with(si)

        self._svc_monitor.vm_manager.check_service = mock.MagicMock(return_value=False)
        svc_monitor.timer_callback(self._svc_monitor)
        self._svc_monitor.vm_manager.create_service.assert_called_with(st, si)

        si.max_instances = 2
        self._svc_monitor.vm_manager.check_service = mock.MagicMock(return_value=True)
        svc_monitor.timer_callback(self._svc_monitor)

    def test_svc_monitor_timer_check_si_netns(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        si.launch_count = 1
        st.virtualization_type = 'network-namespace'
        self._svc_monitor.netns_manager = mock.MagicMock()
        svc_monitor.timer_callback(self._svc_monitor)
        self._svc_monitor.netns_manager.check_service.assert_called_with(si)

    def test_svc_monitor_timer_check_si_vrouter(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        si.launch_count = 1
        st.virtualization_type = 'vrouter-instance'
        self._svc_monitor.vrouter_manager = mock.MagicMock()
        svc_monitor.timer_callback(self._svc_monitor)
        self._svc_monitor.vrouter_manager.check_service.assert_called_with(si)

    def test_svc_monitor_timer_delete_shared_vn(self):
        project_obj = self.add_project('fake-project', 'fake-project')
        net_obj = self.add_vn('svc-vn-left', 'svc-vn-left', project_obj)
        project = config_db.ProjectSM.get('fake-project')
        project.virtual_networks.add('svc-vn-left')

        svc_monitor.timer_callback(self._svc_monitor)
        ServiceMonitorLogger.log_info.assert_any_call(test_utils.AnyStringWith('Deleting vn'))
