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
from cfgm_common.vnc_object_db import VncObjectDBClient
from cfgm_common.vnc_kombu import VncKombuClient


si_add_info = {
    u'oper': u'CREATE', u'uuid': u'fake-instance', u'type': u'service-instance',
    u'fq_name': [u'fake-domain', u'fake-project', u'fake-instance']
}

si_del_info = {
    u'oper': u'DELETE', u'uuid': u'fake-instance', u'type': u'service-instance',
    u'fq_name': [u'fake-domain', u'fake-project', u'fake-instance'],
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
    u'fq_name': [u'fake-domain', u'fake-project', u'left-vn'],
}

vmi_add_info = {
    u'oper': u'CREATE', u'uuid': u'left-vmi', u'type': u'virtual-machine-interface',
    u'fq_name': [u'fake-domain', u'fake-project', u'fake-domain__fake-project__fake-instance__1__left__0'],
}

vmi_del_info = {
    u'oper': u'DELETE', u'uuid': u'left-vmi', u'type': u'virtual-machine-interface',
    u'fq_name': [u'fake-domain', u'fake-project', u'left-vmi'],
    u'obj_dict': {
        u'fq_name': [u'fake-domain', u'fake-project', u'left-vmi'],
        u'uuid': u'left-vmi',
        u'parent_uuid': u'fake-domain:fake-project',
        u'parent_type': u'project'
    }
}

sas_add_info = {
    u'fq_name': [u'default-global-system-config', u'Test-SAS'],
    u'oper': u'CREATE',
    u'request-id': u'req-9977e0e7-910e-41e5-9378-974d2a1820ef',
    u'type': u'service-appliance-set',
    u'uuid': u'sas'
}

sas_del_info = {
    u'fq_name': [u'default-global-system-config', u'Test-SAS'],
    u'obj_dict': {
        u'display_name': u'Test-SAS',
        u'fq_name': [u'default-global-system-config', u'Test-SAS'],
        u'id_perms': {u'created': u'2015-09-23T10:24:56.464362',
            u'creator': None,
            u'description': None,
            u'enable': True,
            u'last_modified': u'2015-09-23T10:24:56.464362',
            u'permissions': {u'group': u'admin',
                u'group_access': 7,
                u'other_access': 7,
                u'owner': u'admin',
                u'owner_access': 7},
            u'user_visible': True,
            u'uuid': {u'uuid_lslong': 11604282682608356844L,
                u'uuid_mslong': 11461005920023169084L}},
        u'parent_type': u'global-system-config',
        u'service_appliance_driver': u'svc_monitor.tests.fake_lb_driver.OpencontrailFakeLoadbalancerDriver',
        u'service_appliance_set_properties': {u'key_value_pair': [{u'key': u'sync_mode',
            u'value': u'replication'},
        {u'key': u'num_snat',
            u'value': u'1'},
        {u'key': u'use_snat',
            u'value': u'True'},
        {u'key': u'global_routed_mode',
            u'value': u'True'}]},
        u'uuid': u'sas'
    },
    u'oper': u'DELETE',
    u'request-id': u'req-9977e0e7-910e-41e5-9378-974d2a1820ef',
    u'type': u'service-appliance-set',
    u'uuid': u'sas'
}

sa_add_info = {
    u'fq_name': [u'default-global-system-config', u'Test-SAS', u'Test-SA'],
    u'oper': u'CREATE',
    u'request-id': u'req-3cd178f7-9662-48ad-8cb5-984c02d4d981',
    u'type': u'service-appliance',
    u'uuid': u'sa'
}

sa_del_info = {
    u'fq_name': [u'default-global-system-config', u'Test-SAS', u'Test-SA'],
    u'obj_dict': {
        u'display_name': u'Test-SA',
        u'fq_name': [u'default-global-system-config', u'Test-SAS', u'Test-SA'],
        u'id_perms': {u'created': u'2015-09-23T10:24:59.261198',
            u'creator': None,
            u'description': None,
            u'enable': True,
            u'last_modified': u'2015-09-23T10:24:59.261198',
            u'permissions': {u'group': u'admin',
                u'group_access': 7,
                u'other_access': 7,
                u'owner': u'admin',
                u'owner_access': 7},
            u'user_visible': True,
            u'uuid': {u'uuid_lslong': 10774623880662702549L,
                u'uuid_mslong': 1841697908979158050}},
        u'parent_type': u'service-appliance-set',
        u'service_appliance_ip_address': u'10.102.44.30',
        u'service_appliance_properties': {u'key_value_pair': []},
        u'service_appliance_user_credentials': {u'password': u'c0ntrail123',
            u'username': u'admin'},
        u'uuid': u'sa'
    },
    u'oper': u'DELETE',
    u'request-id': u'req-3cd178f7-9662-48ad-8cb5-984c02d4d981',
    u'type': u'service-appliance',
    u'uuid': u'sa'
}

pool_add_info = {
    u'fq_name': [u'default-domain', u'admin', u'Test-pool'],
    u'oper': u'CREATE',
    u'request-id': u'req-fad2a313-ed58-48cc-a2b1-3f03a6ca8ca7',
    u'type': u'loadbalancer-pool',
    u'uuid': u'pool'
}

pool_update_info = {
    u'oper': u'UPDATE',
    u'type': u'loadbalancer-pool',
    u'uuid': u'pool'
}

pool_del_info = {
    u'fq_name': [u'default-domain', u'admin', u'Test-pool'],
    u'obj_dict': {
        u'display_name': u'Test-pool',
        u'fq_name': [u'default-domain', u'admin', u'Test-pool'],
        u'id_perms': {u'created': u'2015-09-23T10:17:26.193693',
            u'creator': None,
            u'description': u'Test pool',
            u'enable': True,
            u'last_modified': u'2015-09-23T10:22:18.684195',
            u'permissions': {u'group': u'admin',
                u'group_access': 7,
                u'other_access': 7,
                u'owner': u'neutron',
                u'owner_access': 7},
            u'user_visible': True,
            u'uuid': {u'uuid_lslong': 12634730708897037914L,
                u'uuid_mslong': 8496742968641014440}},
        u'loadbalancer_pool_properties': {u'admin_state': True,
            u'loadbalancer_method': u'ROUND_ROBIN',
            u'protocol': u'TCP',
            u'status': None,
            u'status_description': None,
            u'subnet_id': u'subnet-id'},
        u'loadbalancer_pool_provider': u'Test-SAS',
        u'parent_href': u'http://10.204.216.70:8082/project/e772cab0-bd3f-44ea-91c9-7fea533d3e03',
        u'parent_type': u'project',
        u'parent_uuid': u'fakeproject',
        u'service_appliance_set_refs': [{u'attr': None,
            u'href': u'http://10.204.216.70:8082/service-appliance-set/7859b960-f390-43f1-8a7d-569aaeefef7c',
            u'to': [u'default-global-system-config',
            u'Test-SAS'],
            u'uuid': u'sas'}],
        u'uuid': u'pool'
    },
    u'oper': u'DELETE',
    u'type': u'loadbalancer-pool',
    u'uuid': u'pool'
}

member_add_info = {
    u'fq_name': [u'default-domain', u'admin', u'mypool',
                 u'058f2511-08af-4330-9ea3-119e09408969'],
    u'oper': u'CREATE',
    u'request-id': u'req-5f243860-8512-4ae0-9ff3-55c4fe8844d9',
    u'type': u'loadbalancer-member',
    u'uuid': u'member'
}

member_update_info = {
    u'oper': u'UPDATE',
    u'type': u'loadbalancer-member',
    u'uuid': u'member'
}

member_del_info = {
    u'fq_name': [u'default-domain', u'admin', u'mypool',
                 u'058f2511-08af-4330-9ea3-119e09408969'],
    u'obj_dict': {
        u'display_name': u'058f2511-08af-4330-9ea3-119e09408969',
        u'fq_name': [u'default-domain',
            u'admin',
            u'mypool',
            u'058f2511-08af-4330-9ea3-119e09408969'],
        u'id_perms': {u'created': u'2015-09-23T10:29:24.359873',
            u'creator': None,
            u'description': u'Test pool member',
            u'enable': True,
            u'last_modified': u'2015-09-23T10:29:24.359873',
            u'permissions': {u'group': u'admin',
                u'group_access': 7,
                u'other_access': 7,
                u'owner': u'neutron',
                u'owner_access': 7},
            u'user_visible': True,
            u'uuid': {u'uuid_lslong': 11430999649654180201L,
                u'uuid_mslong': 400579646949638960}},
            u'loadbalancer_member_properties': {u'address': u'1.1.4.5',
                u'admin_state': True,
                u'protocol_port': 91,
                u'status': None,
                u'status_description': None,
                u'weight': 1},
            u'parent_type': u'loadbalancer-pool',
            u'uuid': u'member'
    },
    u'oper': u'DELETE',
    u'request-id': u'req-5f243860-8512-4ae0-9ff3-55c4fe8844d9',
    u'type': u'loadbalancer-member',
    u'uuid': u'member'
}

vip_add_info = {
    u'fq_name': [u'default-domain', u'admin', u'Test-vip'],
    u'oper': u'CREATE',
    u'request-id': u'req-eee836f8-9fd4-4d52-aa73-579afe8c830a',
    u'type': u'virtual-ip',
    u'uuid': u'vip'
}

vip_update_info = {
    u'oper': u'UPDATE',
    u'type': u'virtual-ip',
    u'uuid': u'vip'
}

vip_del_info = {
    u'fq_name': [u'default-domain', u'admin', u'Test-vip'],
    u'obj_dict': {
        u'display_name': u'Test-vip',
        u'fq_name': [u'default-domain', u'admin', u'Test-vip'],
        u'id_perms': {u'created': u'2015-09-23T10:32:33.447634',
            u'creator': None,
            u'description': u'Test vip',
            u'enable': True,
            u'last_modified': u'2015-09-23T10:32:33.447634',
            u'permissions': {u'group': u'admin',
                u'group_access': 7,
                u'other_access': 7,
                u'owner': u'neutron',
                u'owner_access': 7},
            u'user_visible': True,
            u'uuid': {u'uuid_lslong': 9959603007601504245L,
                u'uuid_mslong': 16499857160913372641L}},
            u'loadbalancer_pool_refs': [{u'to': [u'default-domain',
                u'admin',
                u'Test-pool'],
                u'uuid': u'pool'}],
            u'parent_type': u'project',
            u'uuid': u'vip',
            u'virtual_ip_properties': {u'address': u'4.4.4.3',
                u'admin_state': True,
                u'connection_limit': -1,
                u'persistence_cookie_name': None,
                u'persistence_type': None,
                u'protocol': u'TCP',
                u'protocol_port': 91,
                u'status': None,
                u'status_description': None,
                u'subnet_id': u'subnet_id'},
            u'virtual_machine_interface_refs': [{u'to': [u'default-domain',
                u'admin',
                u'e4fb44aa-f8ed-45e1-8a37-9e2acbffeff5'],
                u'uuid': u'dda69314-cb20-486f-a108-5f1067c60c6a'}]
    },
    u'oper': u'DELETE',
    u'request-id': u'req-eee836f8-9fd4-4d52-aa73-579afe8c830a',
    u'type': u'virtual-ip',
    u'uuid': u'vip'
}

class SvcMonitorTest(unittest.TestCase):
    def setUp(self):
        self.args = svc_monitor.parse_args('')
        ServiceMonitorLogger.__init__ = mock.MagicMock(return_value=None)
        ServiceMonitorLogger.log = mock.MagicMock()
        ServiceMonitorLogger.info = mock.MagicMock()
        ServiceMonitorLogger.notice = mock.MagicMock()
        ServiceMonitorLogger.error = mock.MagicMock()
        ServiceMonitorLogger.uve_svc_instance = mock.MagicMock()
        VncObjectDBClient.__init__ = mock.MagicMock()
        VncObjectDBClient._cf_dict = \
            {'service_instance_table':None, 'pool_table':None, \
            'loadbalancer_table': None, 'healthmonitor_table':None}
        VncKombuClient.__init__ = mock.MagicMock(return_value=None)
        self.vnc_mock = mock.MagicMock()

        self._svc_monitor = svc_monitor.SvcMonitor(args=self.args)

        self.add_domain("default-domain", 'default-domain')
        self.vnc_mock.service_template_create = test_utils.st_create
        config_db.DBBaseSM._object_db = mock.MagicMock()
        config_db.DBBaseSM._object_db.object_list = self.db_list
        config_db.DBBaseSM._object_db.object_read = self.db_read

        self._svc_monitor.post_init(self.vnc_mock, self.args)
        self._return_obj = {}

    def tearDown(self):
        config_db.ServiceTemplateSM.reset()
        config_db.ServiceInstanceSM.reset()
        config_db.VirtualNetworkSM.reset()
        config_db.SecurityGroupSM.reset()
        config_db.VirtualMachineSM.reset()
        config_db.VirtualMachineInterfaceSM.reset()
        config_db.ProjectSM.reset()
        config_db.InterfaceRouteTableSM.reset()
        config_db.ServiceApplianceSetSM.reset()
        config_db.ServiceApplianceSM.reset()
        config_db.LoadbalancerSM.reset()
        config_db.LoadbalancerListenerSM.reset()
        config_db.LoadbalancerPoolSM.reset()
        config_db.LoadbalancerMemberSM.reset()
        config_db.VirtualIpSM.reset()
        ServiceMonitorLogger.log.reset_mock()
        ServiceMonitorLogger.info.reset_mock()
        ServiceMonitorLogger.notice.reset_mock()
        ServiceMonitorLogger.error.reset_mock()
        del config_db.DBBaseSM._object_db
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
        config_db.DomainSM._object_db.object_read = mock.Mock(return_value=(True, [dom_dict]))
        config_db.DomainSM.locate(uuid)

    def add_project(self, name, uuid):
        project = Project(name=name)
        proj_dict = self.obj_to_dict(project)
        proj_dict['uuid'] = 'project'
        proj_obj = Project.from_dict(**proj_dict)
        config_db.ProjectSM._object_db.object_read = mock.Mock(return_value=(True, [proj_dict]))
        config_db.ProjectSM.locate(uuid)
        return proj_obj

    def add_irt(self, name, uuid):
        irt = InterfaceRouteTable(name=name)
        irt_dict = self.obj_to_dict(irt)
        irt_dict['uuid'] = 'irt'
        irt_obj = InterfaceRouteTable.from_dict(**irt_dict)
        config_db.InterfaceRouteTableSM._object_db.object_read = mock.Mock(return_value=(True, [irt_dict]))
        config_db.InterfaceRouteTableSM.locate(uuid)
        return irt_obj

    def add_si(self, name, uuid, st):
        si = ServiceInstance(name=name)
        si.set_service_template(st)
        si_dict = self.obj_to_dict(si)
        si_dict['uuid'] = uuid
        si_obj = ServiceInstance.from_dict(**si_dict)
        config_db.ServiceInstanceSM._object_db.object_read = mock.Mock(return_value=(True, [si_dict]))
        config_db.ServiceInstanceSM.locate(uuid)
        return si_obj

    def add_st(self, name, uuid):
        st = ServiceTemplate(name=name)
        st_dict = self.obj_to_dict(st)
        st_dict['uuid'] = uuid
        st_obj = ServiceTemplate.from_dict(**st_dict)
        config_db.ServiceTemplateSM._object_db.object_read = mock.Mock(return_value=(True, [st_dict]))
        config_db.ServiceTemplateSM.locate(uuid)
        return st_obj

    def add_vm(self, name, uuid, si=None, virt_type=None):
        vm = VirtualMachine(name=name)
        if si:
            vm.set_service_instance(si)
            name = si.name + '__' + '1'
            instance_name = "__".join(si.fq_name[:-1] + [name])
            if virt_type:
                vm.set_display_name(instance_name + '__' + virt_type)
            else:
                vm.set_display_name(instance_name)
        vm_dict = self.obj_to_dict(vm)
        vm_dict['uuid'] = uuid
        vm_obj = VirtualMachine.from_dict(**vm_dict)
        config_db.VirtualMachineSM._object_db.object_read = mock.Mock(return_value=(True, [vm_dict]))
        config_db.VirtualMachineSM.locate(uuid)
        return vm_obj

    def add_vn(self, name, uuid, parent_obj):
        network = VirtualNetwork(name=name, parent_obj=parent_obj)
        net_dict = self.obj_to_dict(network)
        net_dict['parent_uuid'] = parent_obj.uuid
        net_dict['uuid'] = uuid
        net_obj = VirtualNetwork.from_dict(**net_dict)
        config_db.VirtualNetworkSM._object_db.object_read = mock.Mock(return_value=(True, [net_dict]))
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
        config_db.VirtualMachineInterfaceSM._object_db.object_read = mock.Mock(return_value=(True, [vmi_dict]))
        config_db.VirtualMachineInterfaceSM.locate(uuid)
        return vmi_obj

    def add_sa(self, name, uuid, sas_obj):
        sa_obj = ServiceAppliance(name, sas_obj)
        sa_obj.set_service_appliance_ip_address("1.2.3.4")
        uci = UserCredentials("James", "Bond")
        sa_obj.set_service_appliance_user_credentials(uci)
        kvp_array = []
        kvp = KeyValuePair("SA-Key1","SA-Value1")
        kvp_array.append(kvp)
        kvp = KeyValuePair("SA-Key2","SA-Value2")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        sa_obj.set_service_appliance_properties(kvps)
        sa_dict = self.obj_to_dict(sa_obj)
        sa_dict['uuid'] = uuid
        sa_dict['parent_uuid'] = 'sas'
        sa_obj = ServiceAppliance.from_dict(**sa_dict)
        self._return_obj['service_appliance'] = sa_dict
        config_db.ServiceApplianceSM.locate('sa')
        return sa_obj


    def add_sas(self, name, uuid):
        sas_obj = ServiceApplianceSet(name)
        sas_obj.set_service_appliance_driver("svc_monitor.tests.fake_lb_driver.OpencontrailFakeLoadbalancerDriver")
        kvp_array = []
        kvp = KeyValuePair("use_snat","True")
        kvp_array.append(kvp)
        kvp = KeyValuePair("global_routed_mode","True")
        kvp_array.append(kvp)
        kvp = KeyValuePair("num_snat","1")
        kvp_array.append(kvp)
        kvp = KeyValuePair("sync_mode","replication")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        sas_obj.set_service_appliance_set_properties(kvps)
        sas_dict = self.obj_to_dict(sas_obj)
        sas_dict['uuid'] = uuid
        sas_obj = ServiceApplianceSet.from_dict(**sas_dict)
        self._return_obj['service_appliance_set'] = sas_dict
        config_db.ServiceApplianceSetSM.locate(uuid)
        return sas_obj
    # end add_sas

    def add_pool(self, name, uuid, proj_obj, sas_obj):
        pool_obj = LoadbalancerPool(name, proj_obj)
        pool_obj.set_service_appliance_set(sas_obj)
        pool_dict = self.obj_to_dict(pool_obj)
        pool_dict['uuid'] = uuid
        pool_dict['display_name'] = name
        pool_dict['parent_uuid'] = proj_obj.uuid
        pool_dict['id_perms'] = {'enable': 'true', 'description': 'Test pool'}
        pool_dict['loadbalancer_pool_provider'] = 'Test-SAS'
        pool_dict['loadbalancer_pool_properties'] = \
            {'protocol': 'TCP', 'subnet_id': 'subnet-id',
             'loadbalancer_method': 'ROUND_ROBIN', 'admin_state': 'true',
             'session_persistence': None, 'persistence_cookie_name': None}
        pool_obj = LoadbalancerPool.from_dict(**pool_dict)
        self._return_obj['loadbalancer_pool'] = pool_dict
        config_db.LoadbalancerPoolSM.locate(uuid)
        return pool_obj
    # end add_pool(self, name, uuid, proj_obj):

    def add_member(self, name, uuid, pool_obj):
        member_obj = LoadbalancerMember(name, pool_obj)
        member_dict = self.obj_to_dict(member_obj)
        member_dict['uuid'] = uuid
        member_dict['parent_uuid'] = pool_obj.uuid
        member_dict['display_name'] = name
        member_dict['id_perms'] = \
            {'enable': 'true', 'description': 'Test pool member'}
        member_dict['loadbalancer_member_properties'] = \
            {'protocol_port': '91', 'address': '1.1.4.5',
             'weight': '1', 'status': 'up', 'admin_state': 'true'}
        member_obj = LoadbalancerMember.from_dict(**member_dict)
        self._return_obj['loadbalancer_member'] = member_dict
        config_db.LoadbalancerMemberSM.locate(uuid)
        return member_obj
    # end add_member(self, name, uuid, pool_obj)

    def add_vip(self, name, uuid, proj_obj, pool_obj):
        vip_obj = VirtualIp(name, proj_obj)
        vip_obj.set_loadbalancer_pool(pool_obj)
        vip_dict = self.obj_to_dict(vip_obj)
        vip_dict['uuid'] = uuid
        vip_dict['parent_uuid'] = proj_obj.uuid
        vip_dict['display_name'] = name
        vip_dict['id_perms'] = {'enable': 'true', 'description': 'Test vip'}
        vip_dict['virtual_ip_properties'] = {'status': 'UP',
                                            'protocol_port': '91',
                                            'subnet_id': 'subnet_id',
                                            'protocol': 'TCP',
                                            'admin_state': 'true',
                                            'connection_limit': -1,
                                            'persistence_type': None,
                                            'persistence_cookie_name': None,
                                            'address': '4.4.4.3'}
        vip_obj = VirtualIp.from_dict(**vip_dict)
        self._return_obj['virtual_ip'] = vip_dict
        config_db.VirtualIpSM.locate('vip')
        return vip_obj
    # end add_vip(self, name, uuid, proj_obj, pool_obj)

    def validate_pool(self, driver_pool, config_pool):
        self.assertEqual(driver_pool['id'], config_pool.uuid)
        self.assertEqual(driver_pool['description'], config_pool.id_perms['description'])
        self.assertEqual(driver_pool['name'], config_pool.name)
        self.assertEqual(driver_pool['vip_id'], config_pool.virtual_ip)
        self.assertEqual(driver_pool['protocol'], config_pool.params['protocol'])
        self.assertEqual(driver_pool['lb_method'], config_pool.params['loadbalancer_method'])
        self.assertEqual(driver_pool['subnet_id'], config_pool.params['subnet_id'])
        self.assertEqual(driver_pool['admin_state_up'], config_pool.params['admin_state'])
        self.assertEqual(driver_pool['tenant_id'], config_pool.parent_uuid)
        self.assertEqual(len(driver_pool['members']), len(config_pool.members))
        self.assertEqual(len(driver_pool['health_monitors']), len(config_pool.loadbalancer_healthmonitors))
    #end

    def validate_pool_member(self, driver_member, config_member):
        self.assertEqual(driver_member['address'], config_member.params['address'])
        self.assertEqual(driver_member['admin_state_up'], config_member.params['admin_state'])
        self.assertEqual(driver_member['protocol_port'], config_member.params['protocol_port'])
        self.assertEqual(driver_member['weight'], config_member.params['weight'])
        self.assertEqual(driver_member['pool_id'], config_member.loadbalancer_pool)
        self.assertEqual(driver_member['id'], config_member.uuid)
    #end

    def validate_vip(self, driver_vip, config_vip):
        self.assertEqual(driver_vip['address'], config_vip.params['address'])
        self.assertEqual(driver_vip['admin_state_up'], config_vip.params['admin_state'])
        self.assertEqual(driver_vip['connection_limit'], config_vip.params['connection_limit'])
        self.assertEqual(driver_vip['protocol_port'], config_vip.params['protocol_port'])
        self.assertEqual(driver_vip['subnet_id'], config_vip.params['subnet_id'])
        self.assertEqual(driver_vip['protocol'], config_vip.params['protocol'])
        self.assertEqual(driver_vip['tenant_id'], config_vip.parent_uuid)
        self.assertEqual(driver_vip['admin_state_up'], config_vip.params['admin_state'])
        self.assertEqual(driver_vip['pool_id'], config_vip.loadbalancer_pool)
    #end

    def object_db_vn_read(self, obj_type, uuids, **kwargs):
        obj = {}
        obj['uuid'] = uuids[0]
        obj['fq_name'] = ['fake-domain', 'fake-project', uuids[0]]
        obj['parent_type'] = 'project'
        return True, [obj]

    def object_db_vmi_read(self, obj_type, uuids, **kwargs):
        obj = {}
        obj['uuid'] = 'left-vmi'
        obj['fq_name'] = ['fake-domain', 'fake-project',
                          'fake-domain__fake-project__fake-instance__1__left__0']
        return True, [obj]

    def db_read(self, obj_type, uuids, **kwargs):
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
        ServiceMonitorLogger.info.assert_any_call(test_utils.AnyStringWith('template created with uuid'))
        self.assertTrue(self._svc_monitor.rabbit._db_resync_done)

    def test_svc_monitor_cgitb(self):
        self._svc_monitor.rabbit._vnc_subscribe_callback(si_add_info)
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
        ServiceMonitorLogger.info.assert_any_call(test_utils.AnyStringWith('Deleting VM'))

    def test_auto_policy_upgrade(self):
        ServiceMonitorLogger.notice.reset_mock()
        ServiceMonitorLogger.error.reset_mock()
        st_obj = self.add_st('netns-snat-template', 'netns-snat-template')
        st = config_db.ServiceTemplateSM.get('netns-snat-template')
        st.virtualization_type = 'network-namespace'
        si_obj = self.add_si('fake-snat-instance', 'fake-snat-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-snat-instance')
        si.params = {'auto_policy': True}
        self._svc_monitor._upgrade_auto_policy(si, st)
        ServiceMonitorLogger.notice.assert_called_with(test_utils.AnyStringWith('snat policy upgraded'))
        ServiceMonitorLogger.error.assert_not_called()

        ServiceMonitorLogger.notice.reset_mock()
        ServiceMonitorLogger.error.reset_mock()
        si.params = {'auto_policy': False}
        self._svc_monitor._upgrade_auto_policy(si, st)
        ServiceMonitorLogger.notice.assert_not_called()
        ServiceMonitorLogger.error.assert_not_called()

    def test_svc_monitor_sas(self):
        def db_read(obj_type, uuids, **kwargs):
            return (True, [self._return_obj[obj_type]])
        config_db.DBBaseSM._object_db.reset()
        config_db.DBBaseSM._object_db.object_read = db_read
        sas_obj = self.add_sas("Test-SAS", 'sas')
        sa_obj = self.add_sa("Test-SA", 'sa', sas_obj)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sas_add_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sa_add_info)
        self.assertTrue('Test-SAS' in self._svc_monitor.loadbalancer_agent._loadbalancer_driver)

        # verify that there is exactly one entry in the DB
        self.assertEqual(len(config_db.ServiceApplianceSM._dict), 1)
        self.assertEqual(len(config_db.ServiceApplianceSetSM._dict), 1)

        self._svc_monitor.rabbit._vnc_subscribe_callback(sa_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sas_del_info)
        self.assertTrue('Test-SAS' not in self._svc_monitor.loadbalancer_agent._loadbalancer_driver)

        self.assertIsNone(config_db.ServiceApplianceSM.get('sa'))
        self.assertIsNone(config_db.ServiceApplianceSetSM.get('sas'))
        # verify that all objs are deleted
        self.assertEqual(len(config_db.ServiceApplianceSM._dict), 0)
        self.assertEqual(len(config_db.ServiceApplianceSetSM._dict), 0)

    def test_svc_monitor_pool_add(self):
        def db_read(obj_type, uuids, **kwargs):
            return (True, [self._return_obj[obj_type]])

        proj_obj = self.add_project('fakeproject', 'fakeproject')

        config_db.DBBaseSM._object_db.reset()
        config_db.DBBaseSM._object_db.object_read = db_read

        sas_obj = self.add_sas("Test-SAS", 'sas')
        sa_obj = self.add_sa("Test-SA", 'sa', sas_obj)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sas_add_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sa_add_info)

        pool_obj = self.add_pool("Test-pool", "pool", proj_obj, sas_obj)
        self._svc_monitor.rabbit._vnc_subscribe_callback(pool_add_info)
        pool = config_db.LoadbalancerPoolSM.get('pool')

        member_obj = self.add_member("member-0", "member", pool_obj)
        member = config_db.LoadbalancerMemberSM.get('member')
        self._svc_monitor.rabbit._vnc_subscribe_callback(member_add_info)

        vip_obj = self.add_vip("Test-vip", "vip", proj_obj, pool_obj)
        vip = config_db.VirtualIpSM.get('vip')
        self._svc_monitor.rabbit._vnc_subscribe_callback(vip_add_info)

        # verify that there is exactly one entry in the DB
        self.assertEqual(len(config_db.ServiceApplianceSM._dict), 1)
        self.assertEqual(len(config_db.ServiceApplianceSetSM._dict), 1)
        self.assertEqual(len(config_db.LoadbalancerMemberSM._dict), 1)
        self.assertEqual(len(config_db.LoadbalancerPoolSM._dict), 1)
        self.assertEqual(len(config_db.VirtualIpSM._dict), 1)

        self.assertTrue('Test-SAS' in self._svc_monitor.loadbalancer_agent._loadbalancer_driver)
        self.validate_pool(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._pools['pool'], pool)
        self.validate_pool_member(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._members['member'],  member)
        self.validate_vip(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._vips['vip'], vip)

        self._svc_monitor.rabbit._vnc_subscribe_callback(member_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(vip_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(pool_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sa_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sas_del_info)

        self.assertIsNone(config_db.ServiceApplianceSM.get('sa'))
        self.assertIsNone(config_db.ServiceApplianceSetSM.get('sas'))
        self.assertIsNone(config_db.LoadbalancerPoolSM.get('pool'))
        self.assertIsNone(config_db.LoadbalancerMemberSM.get('member'))
        self.assertIsNone(config_db.VirtualIpSM.get('vip'))

        # verify that there no entries left
        self.assertEqual(len(config_db.ServiceApplianceSM._dict), 0)
        self.assertEqual(len(config_db.ServiceApplianceSetSM._dict), 0)
        self.assertEqual(len(config_db.LoadbalancerMemberSM._dict), 0)
        self.assertEqual(len(config_db.LoadbalancerPoolSM._dict), 0)
        self.assertEqual(len(config_db.VirtualIpSM._dict), 0)


    def test_svc_monitor_pool_update(self):
        def db_read(obj_type, uuids, **kwargs):
            return (True, [self._return_obj[obj_type]])

        proj_obj = self.add_project('fakeproject', 'fakeproject')

        config_db.DBBaseSM._object_db.reset()
        config_db.DBBaseSM._object_db.object_read = db_read

        sas_obj = self.add_sas("Test-SAS", 'sas')
        sa_obj = self.add_sa("Test-SA", 'sa', sas_obj)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sas_add_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sa_add_info)

        pool_obj = self.add_pool("Test-pool", "pool", proj_obj, sas_obj)
        self._svc_monitor.rabbit._vnc_subscribe_callback(pool_add_info)
        pool = config_db.LoadbalancerPoolSM.get('pool')

        member_obj = self.add_member("member-0", "member", pool_obj)
        member = config_db.LoadbalancerMemberSM.get('member')
        self._svc_monitor.rabbit._vnc_subscribe_callback(member_add_info)

        vip_obj = self.add_vip("Test-vip", "vip", proj_obj, pool_obj)
        self._return_obj['loadbalancer_pool']['virtual_ip_back_refs'] = \
            [{'to': vip_obj.fq_name, 'uuid': vip_obj.uuid}]
        vip = config_db.VirtualIpSM.get('vip')
        self._svc_monitor.rabbit._vnc_subscribe_callback(vip_add_info)

        self.assertTrue('Test-SAS' in self._svc_monitor.loadbalancer_agent._loadbalancer_driver)
        self.validate_pool(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._pools['pool'], pool)
        # verify that there is exactly one entry in the DB
        self.assertEqual(len(config_db.ServiceApplianceSM._dict), 1)
        self.assertEqual(len(config_db.ServiceApplianceSetSM._dict), 1)
        self.assertEqual(len(config_db.LoadbalancerMemberSM._dict), 1)
        self.assertEqual(len(config_db.LoadbalancerPoolSM._dict), 1)
        self.assertEqual(len(config_db.VirtualIpSM._dict), 1)

        self._return_obj['loadbalancer_pool']['loadbalancer_members'] = [{"uuid": "member"}]
        self._return_obj['loadbalancer_pool']['loadbalancer_pool_properties']['loadbalancer_method'] = 'SOURCE_IP'

        self._svc_monitor.rabbit._vnc_subscribe_callback(pool_update_info)
        self.validate_pool(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._pools['pool'], pool)

        self._return_obj['loadbalancer_member']['loadbalancer_member_properties']['weight'] = '999'
        self._svc_monitor.rabbit._vnc_subscribe_callback(member_update_info)
        self.validate_pool(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._pools['pool'], pool)
        self.validate_pool_member(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._members['member'],  member)

        self._return_obj['virtual_ip']['virtual_ip_properties']['protocol_port'] = '777'
        self._svc_monitor.rabbit._vnc_subscribe_callback(member_update_info)
        self.validate_pool(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._pools['pool'], pool)
        self.validate_pool_member(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._members['member'],  member)
        self.validate_vip(self._svc_monitor.loadbalancer_agent._loadbalancer_driver['Test-SAS']._vips['vip'], vip)

        # verify that there is exactly one entry in the DB
        self.assertEqual(len(config_db.ServiceApplianceSM._dict), 1)
        self.assertEqual(len(config_db.ServiceApplianceSetSM._dict), 1)
        self.assertEqual(len(config_db.LoadbalancerMemberSM._dict), 1)
        self.assertEqual(len(config_db.LoadbalancerPoolSM._dict), 1)
        self.assertEqual(len(config_db.VirtualIpSM._dict), 1)

        self._svc_monitor.rabbit._vnc_subscribe_callback(member_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(vip_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(pool_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sa_del_info)
        self._svc_monitor.rabbit._vnc_subscribe_callback(sas_del_info)

        # verify that there no entries left
        self.assertEqual(len(config_db.ServiceApplianceSM._dict), 0)
        self.assertEqual(len(config_db.ServiceApplianceSetSM._dict), 0)
        self.assertEqual(len(config_db.LoadbalancerMemberSM._dict), 0)
        self.assertEqual(len(config_db.LoadbalancerPoolSM._dict), 0)
        self.assertEqual(len(config_db.VirtualIpSM._dict), 0)

    def test_svc_monitor_vm_service_create(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'virtual-machine'

        self._svc_monitor.vm_manager = mock.MagicMock()
        self._svc_monitor.rabbit._vnc_subscribe_callback(si_add_info)
        self._svc_monitor.vm_manager.create_service.assert_called_with(st, si)
        self.assertTrue(si.launch_count==1)
        match_str = "SI %s creation success" % (':').join(si.fq_name)
        ServiceMonitorLogger.info.assert_any_call(match_str)

    def test_svc_monitor_vm_service_delete(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm("fake-vm", 'fake-vm', si_obj, 'virtual-machine')
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.virtual_machines.add('fake-vm')
        vm = config_db.VirtualMachineSM.get('fake-vm')

        self._svc_monitor.vm_manager = mock.MagicMock()
        self._svc_monitor.rabbit._vnc_subscribe_callback(si_del_info)
        self._svc_monitor.vm_manager.delete_service.assert_called_with(vm)
        ServiceMonitorLogger.info.assert_any_call(test_utils.AnyStringWith('Deleted VM'))

    def test_svc_monitor_vm_delayed_vn_add(self):
        st = test_utils.create_test_st(name='fake-template',
            virt_type='virtual-machine', intf_list=[['left', True]])
        si = test_utils.create_test_si(name='fake-instance', count=1,
            intf_list=['left-vn'])
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.service_template = 'fake-template'
        st = config_db.ServiceTemplateSM.get('fake-template')

        config_db.VirtualNetworkSM._object_db.object_read = self.object_db_vn_read
        self._svc_monitor.vm_manager = mock.MagicMock()
        self._svc_monitor.rabbit._vnc_subscribe_callback(vn_add_info)
        self._svc_monitor.vm_manager.create_service.assert_called_with(st, si)
        self.assertTrue(si.launch_count==1)
        match_str = "SI %s creation success" % (':').join(si.fq_name)
        ServiceMonitorLogger.info.assert_any_call(match_str)

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

        config_db.VirtualMachineInterfaceSM._object_db.object_read = self.object_db_vmi_read
        self._svc_monitor.rabbit._vnc_subscribe_callback(vmi_add_info)
        ServiceMonitorLogger.info.assert_any_call(test_utils.AnyStringWith('updated SI'))

    def test_svc_monitor_vmi_del(self):
        project = self.add_project('fake-project', 'fake-project')
        net_obj = self.add_vn('left-vn', 'left-vn', project)
        vmi_obj = self.add_vmi('left-vmi', 'left-vmi', project, net_obj)
        irt_obj = self.add_irt('fake-si-uuid left', 'fake-irt')
        vmi = config_db.VirtualMachineInterfaceSM.get('left-vmi')
        vmi.interface_route_table = 'fake-irt'

        config_db.VirtualMachineInterfaceSM._object_db.object_read = self.object_db_vmi_read
        self._svc_monitor.rabbit._vnc_subscribe_callback(vmi_del_info)
        self.vnc_mock.interface_route_table_delete.assert_called_with(id='fake-irt')

    def test_svc_monitor_snat_service_create(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'network-namespace'

        self._svc_monitor.netns_manager = mock.MagicMock()
        self._svc_monitor.rabbit._vnc_subscribe_callback(si_add_info)
        self._svc_monitor.netns_manager.create_service.assert_called_with(st, si)
        self.assertTrue(si.launch_count==1)
        match_str = "SI %s creation success" % (':').join(si.fq_name)
        ServiceMonitorLogger.info.assert_any_call(match_str)

    def test_svc_monitor_snat_service_delete(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm("fake-vm", 'fake-vm', si_obj, 'network-namespace')
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.virtual_machines.add('fake-vm')
        vm = config_db.VirtualMachineSM.get('fake-vm')

        self._svc_monitor.netns_manager = mock.MagicMock()
        self._svc_monitor.rabbit._vnc_subscribe_callback(si_del_info)
        self._svc_monitor.netns_manager.delete_service.assert_called_with(vm)
        ServiceMonitorLogger.info.assert_any_call(test_utils.AnyStringWith('Deleted VM'))

    def test_svc_monitor_vrouter_service_create(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'vrouter-instance'

        self._svc_monitor.vrouter_manager = mock.MagicMock()
        self._svc_monitor.rabbit._vnc_subscribe_callback(si_add_info)
        self._svc_monitor.vrouter_manager.create_service.assert_called_with(st, si)
        self.assertTrue(si.launch_count==1)
        match_str = "SI %s creation success" % (':').join(si.fq_name)
        ServiceMonitorLogger.info.assert_any_call(match_str)

    def test_svc_monitor_vrouter_service_delete(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm("fake-vm", 'fake-vm', si_obj, 'vrouter-instance')
        si = config_db.ServiceInstanceSM.get('fake-instance')
        si.virtual_machines.add('fake-vm')
        vm = config_db.VirtualMachineSM.get('fake-vm')

        self._svc_monitor.vrouter_manager = mock.MagicMock()
        self._svc_monitor.rabbit._vnc_subscribe_callback(si_del_info)
        self._svc_monitor.vrouter_manager.delete_service.assert_called_with(vm)
        ServiceMonitorLogger.info.assert_any_call(test_utils.AnyStringWith('Deleted VM'))

    def test_svc_monitor_timer_delete_vms(self):
        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        vm_obj = self.add_vm("fake-vm", 'fake-vm', si_obj, 'virtual-machine')
        vm = config_db.VirtualMachineSM.get('fake-vm')
        config_db.ServiceInstanceSM.delete('fake-instance')
        vm.service_instance = 'non-existent-instance'

        svc_monitor.timer_callback(self._svc_monitor)
        ServiceMonitorLogger.info.assert_any_call(test_utils.AnyStringWith('Deleting VM'))

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
        self._svc_monitor.vm_manager.create_service.assert_called_with(st, si)

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
        ServiceMonitorLogger.info.assert_any_call(test_utils.AnyStringWith('Deleting vn'))

    def test_svc_monitor_restart_vm_create(self):
        def db_read(obj_type, uuids, **kwargs):
            obj = {}
            obj['uuid'] = uuids[0]
            if obj_type == 'service_template':
                obj['fq_name'] = 'default-domain:fake-template'
                return (True, [obj])
            elif obj_type == 'service_instance':
                obj['fq_name'] = 'default-domain:default-project:fake-instance'
                return (True, [obj])
            else:
                return (False, None)

        def db_list(obj_type):
            if obj_type == 'service_template':
                fq_name = 'default-domain:fake-template'
                return (True, [([fq_name], obj_type)])
            elif obj_type == 'service_instance':
                fq_name = 'default-domain:default-project:fake-instance'
                return (True, [([fq_name], obj_type)])
            else:
                return (False, None)

        config_db.DBBaseSM._object_db.reset()
        config_db.DBBaseSM._object_db.object_list = db_list
        config_db.DBBaseSM._object_db.object_read = db_read
        self._svc_monitor.create_service_instance = mock.MagicMock()

        st_obj = self.add_st('fake-template', 'fake-template')
        si_obj = self.add_si('fake-instance', 'fake-instance', st_obj)
        si = config_db.ServiceInstanceSM.get('fake-instance')
        st = config_db.ServiceTemplateSM.get('fake-template')
        st.virtualization_type = 'virtual-machine'
        st.params = {'service_type': 'firewall'}
        self._svc_monitor.post_init(self.vnc_mock, self.args)
        self._svc_monitor.create_service_instance.assert_called_with(si)
