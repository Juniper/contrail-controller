import mock
from mock import patch
import unittest
from cfgm_common.vnc_db import DBBase
from svc_monitor import config_db
from svc_monitor import loadbalancer_agent
from vnc_api.vnc_api import *
import argparse
import ConfigParser

class F5LBTest(unittest.TestCase):
    def setUp(self):
        self.vnc_lib = mock.Mock()
        self.cassandra = mock.Mock()
        self.logger = mock.Mock()
        self.svc = mock.Mock()

        mocked_gsc = mock.MagicMock()
        mocked_gsc.uuid = 'fake-gsc-uuid'
        self.vnc_lib.global_system_config_read.return_value = mocked_gsc
        def mock_kv_retrieve(subnet_id):
            if subnet_id == "pool_subnet_id":
                return "fake-pool-vn 40.1.1.0/24"
            elif subnet_id == "vip_subnet_id":
                return "fake-vip-vn 1.1.1.0/24"
            self.assertTrue(False)
        #end
        self.vnc_lib.kv_retrieve = mock.Mock(side_effect=mock_kv_retrieve)
        self.vnc_lib.service_appliance_set_create.return_value = "opencontrail"

        self._mock_bigip_interfaces = None
        self._mock_BigIp = None

        self._db = {}
        def read_db(id):
            if id in self._db:
                return self._db[id].get('driver_info', None)

        def put_db(id, data):
            from copy import deepcopy
            self._db[id] = {'driver_info': deepcopy(data)}

        def remove_db(id, data=None):
            if data is None:
                del self._db[id]
                return
            if self._db[id][data[0]]:
                del self._db[id][data[0]]
        self.cassandra.pool_driver_info_get = mock.Mock(side_effect=read_db)
        self.cassandra.pool_driver_info_insert = mock.Mock(side_effect=put_db)
        self.cassandra.pool_remove = mock.Mock(side_effect=remove_db)

        conf_parser = argparse.ArgumentParser(add_help=False)
        config = ConfigParser.SafeConfigParser({'admin_token': None})
        self._args, remaining_argv = conf_parser.parse_known_args()
        self._args.config_sections = config

        def sas_read_side_effect(obj_type, uuids, **kwargs):
            if obj_type == 'service_appliance_set':
                return (True, [{
                 'fq_name': ['default-global-system-config', 'opencontrail'],
                 'service_appliance_driver': 'svc_monitor.services.loadbalancer\
.drivers.ha_proxy.driver.OpencontrailLoadbalancerDriver'
                   }])
            return (False, None)
        DBBase.init(self.svc, None, self.cassandra)
        config_db.ServiceApplianceSetSM._cassandra.object_read = \
                         mock.Mock(side_effect=sas_read_side_effect)

        # return NoIdError exception for first query
        def no_id_side_effect(fq_name):
            raise NoIdError("xxx")
        self.vnc_lib.service_appliance_set_read = \
            mock.Mock(side_effect=no_id_side_effect)

        self.lb_agent = loadbalancer_agent.LoadbalancerAgent(self.svc, 
            self.vnc_lib, self.cassandra, self._args)
        self.svc.loadbalancer_agent = self.lb_agent
        sas = config_db.ServiceApplianceSetSM.get('opencontrail')
        self.assertEqual(sas.driver,
"svc_monitor.services.loadbalancer.drivers.ha_proxy.driver.\
OpencontrailLoadbalancerDriver")
        sas.add()
        DBBase.init(self.svc, None, self.cassandra)
        config_db.ServiceApplianceSetSM._cassandra.object_read = \
                         mock.Mock(side_effect=sas_read_side_effect)

        import sys
        sys.modules['f5'] = mock.Mock()
        sys.modules['f5.bigip'] = mock.Mock()
        self.create_f5_service_appliance_set()


    # end setUp


    def create_f5_service_appliance_set(self):
        sas_obj = {}
        sas_obj['fq_name'] = ["default-global-system-config", "f5"]
        sas_obj['uuid'] = 'f5-sas'
        sas_obj['display_name'] = "f5"
        sas = config_db.ServiceApplianceSetSM.locate(sas_obj['uuid'], sas_obj)
        sas.kvpairs = [{'key': 'sync_mode', 'value': 'replication'},
                       {'key': 'num_snat', 'value': '1'},
                       {'key': 'global_routed_mode', 'value': 'True'},
                       {'key': 'vip_vlan', 'value': 'access'},
                       {'key': 'use_snat', 'value': 'True'}]
        sas.ha_mode = "standalone"
        sas.driver = "svc_monitor.services.loadbalancer.drivers.f5.f5_driver.OpencontrailF5LoadbalancerDriver"

        sa_obj = {}
        sa_obj['fq_name'] = ["default-global-system-config", "f5", "bigip"]
        sa_obj['uuid'] = 'bigip'
        sa_obj['display_name'] = 'bigip'
        sa_obj['parent_uuid'] = 'f5-sas'
        sa_obj['service_appliance_ip_address'] = "1.1.1.1"
        sa_obj['service_appliance_user_credentials'] = {'username': "admin", 
                                                      'password': "contrail123"}
        sa = config_db.ServiceApplianceSM.locate(sa_obj['uuid'], sa_obj)

        def test_decorate_name(name1, name2):
            return name1+'_'+name2

        bigip_patcher = \
            mock.patch('svc_monitor.services.loadbalancer.drivers.f5.f5_driver.f5_bigip.BigIP')
        self._mock_BigIp = bigip_patcher.start()
        self._mock_BigIp.return_value.cluster.get_traffic_groups.return_value = []

        bigip_interface_patcher = \
            mock.patch('svc_monitor.services.loadbalancer.drivers.f5.f5_driver.bigip_interfaces')
        self._mock_bigip_interfaces = bigip_interface_patcher.start()
        self._mock_bigip_interfaces.decorate_name = \
            mock.Mock(side_effect=test_decorate_name)
        sas.add()

    def tearDown(self):
        self._mock_BigIp.reset_mock()
        config_db.ServiceApplianceSetSM.delete("opencontrail")
        config_db.ServiceApplianceSetSM.delete("f5-sas")
        config_db.ServiceApplianceSM.delete("bigip")
        config_db.LoadbalancerPoolSM.reset()
        config_db.VirtualIpSM.reset()
        config_db.InstanceIpSM.reset()
        config_db.VirtualMachineInterfaceSM.reset()
        config_db.VirtualNetworkSM.reset()
        config_db.ProjectSM.reset()
    # end tearDown

    def create_pool(self, uuid, fq_name_str, project=None, vip=None, hm=None):
        pool_network = self.create_vn("fake-pool-vn", "fake-pool-vn", project)
        pool_obj = {}
        pool_obj['fq_name'] = fq_name_str.split(':')
        pool_obj['uuid'] = uuid
        pool_obj['display_name'] = fq_name_str
        pool_obj['parent_uuid'] = 'tenant'
        pool_obj['id_perms'] = {'enable': 'true', 'description': 'Test pool'}
        pool_obj['loadbalancer_pool_provider'] = 'f5'
        pool_obj['loadbalancer_pool_properties'] = \
            {'protocol': 'HTTP', 'subnet_id': 'pool_subnet_id',
             'loadbalancer_method': 'ROUND_ROBIN', 'admin_state': True,
             'session_persistence': None, 'persistence_cookie_name': None}
        if vip:
            pool_obj['virtual_ip_back_refs']=[{'uuid': vip.uuid}]
        if hm:
            pool_obj['loadbalancer_healthmonitor_refs']=[{'uuid': hm.uuid}]
        pool = config_db.LoadbalancerPoolSM.locate(pool_obj['uuid'], pool_obj)
        return pool
    # end create_pool

    def create_hm_obj(self, fq_name_str):
        hm_obj = {}
        hm_obj['fq_name'] = fq_name_str.split(':')
        hm_obj['uuid'] = fq_name_str
        hm_obj['display_name'] = fq_name_str
        hm_obj['parent_uuid'] = 'tenant'
        hm_obj['id_perms'] = {'enable': 'true', 'description': 'Test pool'}
        hm_obj['loadbalancer_healthmonitor_properties'] = {'delay': '5',
                                                    'expected_codes': '200',
                                                    'max_retries': '200',
                                                    'http_method': 'GET',
                                                    'timeout': '2',
                                                    'url_path': '/',
                                                    'monitor_type': 'HTTP',
                                                    'admin_state': True}
        return hm_obj
    #end create_hm_obj

    def create_hm(self, fq_name_str):
        hm_obj = self.create_hm_obj(fq_name_str)
        hm = config_db.HealthMonitorSM.locate(hm_obj['uuid'], hm_obj)
        return hm
    # end create_hm

    def update_pool(self, pool_obj, vip=None):
        pool_obj.params['loadbalancer_method'] = 'LEAST_CONNECTIONS'
        pool_obj.params['protocol'] = 'HTTPS'
        pool_obj.params['admin_state'] = False
    # end update_pool

    def update_vip(self, vip_obj, pool=None):
        vip_obj.params['connection_limit'] = '100'
        vip_obj.params['persistence_type'] = 'always'
        vip_obj.params['admin_state'] = False
    # end update_vip

    def create_pool_members(self, pool_name, num_members):
        for i in range(num_members):
            self.create_pool_member(pool_name, 'member_'+str(i),
                                    '10.1.1.'+str(i))
    # end create_pool_members

    def create_pool_member(self, pool_name, member_name, member_address):
        pool_member_obj = {}
        pool_member_obj['fq_name'] = member_name
        pool_member_obj['uuid'] = member_name
        pool_member_obj['display_name'] = member_name
        pool_member_obj['parent_uuid'] = pool_name
        pool_member_obj['id_perms'] = \
            {'enable': 'true', 'description': 'Test pool member'}
        pool_member_obj['loadbalancer_member_properties'] = \
            {'protocol_port': '80', 'address': member_address,
             'weight': '1', 'status': 'up', 'admin_state': True}
        member = config_db.LoadbalancerMemberSM.locate(pool_member_obj['uuid'],
            pool_member_obj)
    # end create_pool_member

    def create_project(self, name, uuid):
        project = Project(name=name, fq_name=["default-domain", name])
        project.uuid = uuid
        proj_dict = self.obj_to_dict(project)
        config_db.ProjectSM.locate(uuid, proj_dict)
        return project
    # end create_project

    def create_vn(self, name, uuid, parent_obj):
        network = VirtualNetwork(name=name, parent_obj=parent_obj)
        network.uuid = uuid
        net_dict = self.obj_to_dict(network)
        config_db.VirtualNetworkSM.locate(uuid, net_dict)
        return network
    # end create_vn

    def obj_to_dict(self, obj):
        def to_json(obj):
            if hasattr(obj, 'serialize_to_json'):
                return obj.serialize_to_json(obj.get_pending_updates())
            else:
                return dict((k, v) for k, v in obj.__dict__.iteritems())

        return json.loads(json.dumps(obj, default=to_json))
    # end obj_to_dict

    def create_vmi(self, name, uuid, parent_obj, net_obj):
        vmi = VirtualMachineInterface(name=name, parent_obj=parent_obj)
        vmi.set_virtual_network(net_obj)
        vmi.uuid = uuid
        vmi_dict = self.obj_to_dict(vmi)
        config_db.VirtualMachineInterfaceSM.locate(uuid, vmi_dict)
        return vmi
    # end create_vmi

    def create_iip(self, name, uuid, ip, net_obj, vmi_obj):
        iip = InstanceIp(name=name, instance_ip_address=ip,
                         instance_ip_family="v4")
        iip.set_virtual_network(net_obj)
        iip.set_virtual_machine_interface(vmi_obj)
        iip.uuid = uuid
        iip_dict = self.obj_to_dict(iip)
        config_db.InstanceIpSM.locate(uuid, iip_dict)
        return iip
    # end create_iip

    def create_vip(self, vip, project):
        vip_obj = {}
        vip_obj['fq_name'] = vip.split(':')
        vip_obj['uuid'] = vip
        vip_obj['display_name'] = vip
        vip_obj['id_perms'] = {'enable': 'true', 'description': 'Test pool'}
        vip_obj['virtual_ip_properties'] = {'status': 'UP',
                                            'protocol_port': '80',
                                            'subnet_id': 'vip_subnet_id',
                                            'protocol': 'HTTP',
                                            'admin_state': True,
                                            'connection_limit': -1,
                                            'persistence_type': None,
                                            'persistence_cookie_name': None,
                                            'address': '1.1.1.1'}
        network = self.create_vn("fake-vip-vn", "fake-vip-vn", project)
        vmi = self.create_vmi("vmi", "vmi", project, network)
        iip = self.create_iip("iip", "iip", "1.1.1.1", network, vmi)
        vip_vnc = VirtualIp.from_dict(**vip_obj)
        vip_vnc.set_virtual_machine_interface(vmi)
        vip_obj = self.obj_to_dict(vip_vnc)
        vip_obj['parent_uuid'] = 'tenant'
        vip = config_db.VirtualIpSM.locate(vip, vip_obj)
        return vip
    # end create_vip

    def test_add_delete_pool_with_members_vip(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 5)
        pool.add()
        self.assertEqual(len(self._db), 1)
        self.assertTrue('test-lb-pool' in self._db)

        # Ensure we call the BigIp with correct ip, user name and password
        self._mock_BigIp.assert_called_with('1.1.1.1', 'admin', 'contrail123',
                                            5, True, True)
        self._mock_BigIp.return_value.pool.create.assert_called_with(
            description='test-lb-pool:Test pool', folder='tenant',
            lb_method='ROUND_ROBIN', name='test-lb-pool')

        expected_calls_to_add_member = \
          [mock.call(folder='tenant', ip_address='10.1.1.4%0',
                name='test-lb-pool', no_checks=True, port=80),
           mock.call(folder='tenant', ip_address='10.1.1.3%0',
                name='test-lb-pool', no_checks=True, port=80),
           mock.call(folder='tenant', ip_address='10.1.1.2%0',
                name='test-lb-pool', no_checks=True, port=80),
           mock.call(folder='tenant', ip_address='10.1.1.1%0',
                name='test-lb-pool', no_checks=True, port=80),
           mock.call(folder='tenant', ip_address='10.1.1.0%0',
                name='test-lb-pool', no_checks=True, port=80)]
        call_list = self._mock_BigIp.return_value.pool.add_member.call_args_list
        self.assertEqual(call_list, expected_calls_to_add_member)

        expected_calls_to_enable_member = expected_calls_to_add_member
        call_list = \
           self._mock_BigIp.return_value.pool.enable_member.call_args_list
        self.assertEqual(call_list, expected_calls_to_enable_member)


        expected_calls_to_set_member_ratio = \
          [mock.call(folder='tenant', ip_address='10.1.1.4%0',
                name='test-lb-pool', no_checks=True, port=80, ratio=1),
           mock.call(folder='tenant', ip_address='10.1.1.3%0',
                name='test-lb-pool', no_checks=True, port=80, ratio=1),
           mock.call(folder='tenant', ip_address='10.1.1.2%0',
                name='test-lb-pool', no_checks=True, port=80, ratio=1),
           mock.call(folder='tenant', ip_address='10.1.1.1%0',
                name='test-lb-pool', no_checks=True, port=80, ratio=1),
           mock.call(folder='tenant', ip_address='10.1.1.0%0',
                name='test-lb-pool', no_checks=True, port=80, ratio=1)]
        call_list = \
            self._mock_BigIp.return_value.pool.set_member_ratio.call_args_list
        self.assertEqual(call_list, expected_calls_to_set_member_ratio)

        self._mock_BigIp.return_value.virtual_server.create.assert_called_with(
            folder='tenant', ip_address='1.1.1.1%0', mask='255.255.255.255',
            name='vip', port=80, preserve_vlan_name=True, protocol='HTTP',
            snat_pool=None, traffic_group='/Common/traffic-group-1',
            use_snat=True, vlan_name='access')
        self._mock_BigIp.return_value.virtual_server.set_description.assert_called_with(
            description='vip:Test pool', folder='tenant', name='vip')
        self._mock_BigIp.return_value.virtual_server.set_pool.assert_called_with(
            folder='tenant', name='vip', pool_name='test-lb-pool')
        self._mock_BigIp.return_value.virtual_server.enable_virtual_server.assert_called_with(
            folder='tenant', name='vip')

        # Cleanup
        for i in range(5):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')

        self._mock_BigIp.return_value.virtual_server.delete.assert_called_with(
            folder='tenant', name='vip')

        expected_calls_to_remove_member = \
          [mock.call(folder='tenant', ip_address='10.1.1.4%0',
                name='test-lb-pool', port=80),
           mock.call(folder='tenant', ip_address='10.1.1.3%0',
                name='test-lb-pool', port=80),
           mock.call(folder='tenant', ip_address='10.1.1.2%0',
                name='test-lb-pool', port=80),
           mock.call(folder='tenant', ip_address='10.1.1.1%0',
                name='test-lb-pool', port=80),
           mock.call(folder='tenant', ip_address='10.1.1.0%0',
                name='test-lb-pool', port=80)]
        call_list = self._mock_BigIp.return_value.pool.remove_member.call_args_list
        self.assertEqual(call_list, expected_calls_to_remove_member)
        self._mock_BigIp.return_value.pool.delete.assert_called_with(
            folder='tenant', name='test-lb-pool')
        self.assertEqual(len(self._db), 0)
    # end test_add_delete_pool_with_members_vip

    def test_add_delete_pool_with_members_vip_hm(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)

	hm_obj = self.create_hm_obj("test-hm")
        hm_obj['loadbalancer_pool_back_refs']=[{'uuid': pool.uuid}]
        hm = config_db.HealthMonitorSM.locate(hm_obj['uuid'], hm_obj)

        self.create_pool_members("test-lb-pool", 2)
        pool.add()

        self._mock_BigIp.return_value.monitor.create.assert_called_with(
          folder='tenant', interval='5', mon_type='HTTP', name='test-hm', 
          recv_text=None, send_text=None, timeout=400)
        self._mock_BigIp.return_value.pool.add_monitor.assert_called_with(
          folder='tenant', monitor_name='test-hm', name='test-lb-pool')
        self._mock_BigIp.return_value.monitor.set_send_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          send_text='GET / HTTP/1.0\\r\\n\\r\\n')
        self._mock_BigIp.return_value.monitor.set_recv_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          recv_text='HTTP/1\\.(0|1) 200')
        self.assertEqual(len(self._db), 1)
        self.assertTrue('test-lb-pool' in self._db)

        # Cleanup
        for i in range(2):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
        config_db.HealthMonitorSM.delete('test-hm')

        self._mock_BigIp.return_value.monitor.delete.assert_called_with(
            folder='tenant', mon_type='HTTP', name='test-hm')
        self.assertEqual(len(self._db), 0)
    # end test_add_delete_pool_with_members_vip_hm

    def test_update_pool(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 2)
        pool.add()
        
        self._mock_BigIp.reset_mock()
        pool.id_perms['description'] = 'updated'
        pool.add()
        self._mock_BigIp.return_value.pool.set_description.assert_called_with(
            description='test-lb-pool:updated', folder='tenant',
            name='test-lb-pool')

        self._mock_BigIp.reset_mock()
        pool.params['loadbalancer_method'] = 'LEAST_CONNECTIONS'
        pool.add()
        self._mock_BigIp.return_value.pool.set_lb_method.assert_called_with(
            folder='tenant', lb_method='LEAST_CONNECTIONS', name='test-lb-pool')

        # Cleanup
        for i in range(2):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_pool

    # Test the case where vip is deleted before the pool
    def test_update_pool_1(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 2)

        pool.add()


        # Delete the VIP
        config_db.VirtualIpSM.delete('vip')

        # update the pool with no vip
        pool.add()

        self._mock_BigIp.return_value.virtual_server.delete.assert_called_with(
            folder='tenant', name='vip')

        expected_calls_to_remove_member = \
          [mock.call(folder='tenant', ip_address='10.1.1.1%0',
                name='test-lb-pool', port=80),
           mock.call(folder='tenant', ip_address='10.1.1.0%0',
                name='test-lb-pool', port=80)]
        call_list = self._mock_BigIp.return_value.pool.remove_member.call_args_list
        self.assertEqual(call_list, expected_calls_to_remove_member)
        self._mock_BigIp.return_value.pool.delete.assert_called_with(
            folder='tenant', name='test-lb-pool')
        self.assertEqual(len(self._db), 1)

        # Cleanup
        for i in range(2):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))

        self._mock_BigIp.reset_mock()

        self.assertFalse(self._mock_BigIp.return_value.pool.delete.called)
        self.assertFalse(self._mock_BigIp.return_value.pool.remove_member.called)
        self.assertFalse(self._mock_BigIp.return_value.virtual_server.delete.called)
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_pool


    def test_update_pool_members_add_delete(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 2)
        pool.add()
        
        self._mock_BigIp.reset_mock()
        self.create_pool_members("test-lb-pool", 3)
        pool.add()
        # Ensure that only the new member is added 
        self._mock_BigIp.return_value.pool.add_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.2%0', name='test-lb-pool',
            no_checks=True, port=80)
        self._mock_BigIp.return_value.pool.enable_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.2%0', name='test-lb-pool',
            no_checks=True, port=80)
        self._mock_BigIp.return_value.pool.set_member_ratio.assert_called_with(
            folder='tenant', ip_address='10.1.1.2%0', name='test-lb-pool',
            no_checks=True, port=80, ratio=1)

        # Delete last two members
        self._mock_BigIp.reset_mock()
        for i in range(2):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i+1))
        pool.add()

        expected_calls_to_remove_member =\
          [mock.call(folder='tenant', ip_address='10.1.1.2%0',
                name='test-lb-pool', port=80),
           mock.call(folder='tenant', ip_address='10.1.1.1%0',
                name='test-lb-pool', port=80)]
        call_list = self._mock_BigIp.return_value.pool.remove_member.call_args_list
        self.assertEqual(call_list, expected_calls_to_remove_member)
        
        # Cleanup
        config_db.LoadbalancerMemberSM.delete('member_0')
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_pool_members_add_delete

    def test_update_pool_member_props(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 2)
        pool.add()
        
        # Validate member ratio update
        self._mock_BigIp.reset_mock()
        member = config_db.LoadbalancerMemberSM.get('member_0')
        member.params['weight'] = 20
        pool.add()
        self._mock_BigIp.return_value.pool.set_member_ratio.assert_called_with(
            folder='tenant', ip_address='10.1.1.0%0', name='test-lb-pool',
            no_checks=True, port=80, ratio=20)
        self._mock_BigIp.reset_mock()

        # Validate member admin_state update
        member = config_db.LoadbalancerMemberSM.get('member_1')
        member.params['admin_state'] = False
        pool.add()
        self._mock_BigIp.return_value.pool.disable_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.1%0', name='test-lb-pool',
            no_checks=True, port=80)
        self._mock_BigIp.reset_mock()

        member = config_db.LoadbalancerMemberSM.get('member_1')
        member.params['admin_state'] = True
        pool.add()
        self._mock_BigIp.return_value.pool.enable_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.1%0', name='test-lb-pool',
            no_checks=True, port=80)
        # Cleanup
        for i in range(2):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_pool_member_props

    def test_update_pool_members_add_delete_update(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        for i in range(2):
            self.create_pool_member("test-lb-pool", 'member_'+str(i),
                                    '10.1.1.'+str(i))
        pool.add()
        
        self._mock_BigIp.reset_mock()
        # Existing member 1,2
        # New member 3
        # delete the member 1
        # Final meber 2,3
        self.create_pool_member("test-lb-pool", 'member_2',
                                    '10.1.1.2')
        config_db.LoadbalancerMemberSM.delete('member_0')
        member = config_db.LoadbalancerMemberSM.get('member_1')
        member.params['admin_state'] = False
        pool.add()


        # validate
        # member_1 updated with admin state disable
        self._mock_BigIp.return_value.pool.disable_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.1%0', name='test-lb-pool',
            no_checks=True, port=80)
        # member_0 removed
        self._mock_BigIp.return_value.pool.remove_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.0%0', name='test-lb-pool',
            port=80)
        # member_2 added
        self._mock_BigIp.return_value.pool.add_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.2%0', name='test-lb-pool',
            no_checks=True, port=80)
        self._mock_BigIp.return_value.pool.enable_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.2%0', name='test-lb-pool',
            no_checks=True, port=80)
        self._mock_BigIp.return_value.pool.set_member_ratio.assert_called_with(
            folder='tenant', ip_address='10.1.1.2%0', name='test-lb-pool',
            no_checks=True, port=80, ratio=1)
        # Cleanup
        config_db.LoadbalancerMemberSM.delete('member_1')
        config_db.LoadbalancerMemberSM.delete('member_2')
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_pool_members_add_delete

    def test_pool_for_tcp(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        vip.params['protocol'] = 'FTP'
        vip.params['protocol_port'] = '22'
        vip.params['connection_limit'] = '12'
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        pool.params['protocol'] = 'FTP'
        pool.params['loadbalancer_method'] = 'SOURCE_IP'
        self.create_pool_members("test-lb-pool", 1)
        member_0 = config_db.LoadbalancerMemberSM.get('member_0')
        member_0.params['protocol_port'] = '23'
        pool.add()
        
        # Validate calls with correct port
        self._mock_BigIp.return_value.pool.create.assert_called_with(
            description='test-lb-pool:Test pool', folder='tenant', 
            lb_method='SOURCE_IP', name='test-lb-pool')
        self._mock_BigIp.return_value.pool.add_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.0%0', name='test-lb-pool', 
            no_checks=True, port=23)
        self._mock_BigIp.return_value.pool.enable_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.0%0', name='test-lb-pool', 
            no_checks=True, port=23)
        self._mock_BigIp.return_value.pool.set_member_ratio.assert_called_with(
            folder='tenant', ip_address='10.1.1.0%0', name='test-lb-pool', 
            no_checks=True, port=23, ratio=1)
        self._mock_BigIp.return_value.virtual_server.create.assert_called_with(
            folder='tenant', ip_address='1.1.1.1%0', mask='255.255.255.255', 
            name='vip', port=22, preserve_vlan_name=True, protocol='FTP', 
            snat_pool=None, traffic_group='/Common/traffic-group-1', use_snat=True, 
            vlan_name='access')
        self._mock_BigIp.return_value.virtual_server.set_description.assert_called_with(
            description='vip:Test pool', folder='tenant', name='vip')
        self._mock_BigIp.return_value.virtual_server.set_pool.assert_called_with(
            folder='tenant', name='vip', pool_name='test-lb-pool')
        self._mock_BigIp.return_value.virtual_server.enable_virtual_server.assert_called_with(
            folder='tenant', name='vip')
        self._mock_BigIp.return_value.virtual_server.remove_all_persist_profiles.assert_called_with(
            folder='tenant', name='vip')
        self._mock_BigIp.return_value.virtual_server.set_connection_limit.assert_called_with(
            connection_limit=12, folder='tenant', name='vip')
        self._mock_BigIp.reset_mock()

        # Cleanup
        config_db.LoadbalancerMemberSM.delete('member_0')
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')

        self._mock_BigIp.return_value.virtual_server.delete.assert_called_with(
            folder='tenant', name='vip')
        self._mock_BigIp.return_value.pool.remove_member.assert_called_with(
            folder='tenant', ip_address='10.1.1.0%0', name='test-lb-pool', port=23)
        self._mock_BigIp.return_value.pool.delete.assert_called_with(
            folder='tenant', name='test-lb-pool')
        self._mock_BigIp.return_value.arp.delete_all.assert_called_with(
            folder='tenant')
        self._mock_BigIp.return_value.decorate_folder.assert_called_with('tenant')
        self.assertTrue(self._mock_BigIp.return_value.system.delete_folder.called)
    # end test_update_vip

    def test_update_vip(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 2)
        pool.add()
        
        # Validate vip update
        self._mock_BigIp.reset_mock()
        vip.id_perms['description'] = "New Description"
        pool.add()
        self._mock_BigIp.return_value.virtual_server.set_description.assert_called_with(
            description='vip:New Description', folder='tenant', name='vip')

        self._mock_BigIp.reset_mock()
        vip.params['admin_state'] = False
        pool.add()
        self._mock_BigIp.return_value.virtual_server.disable_virtual_server.assert_called_with(
            folder='tenant', name='vip')

        vip.params['admin_state'] = True
        pool.add()
        self._mock_BigIp.return_value.virtual_server.enable_virtual_server.assert_called_with(
            folder='tenant', name='vip')

        self._mock_BigIp.reset_mock()
        vip.params['connection_limit'] = '100'
        pool.add()

        # Cleanup
        for i in range(2):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_vip

    def test_update_vip_persistance_type(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        vip.params['persistence_type'] = 'SOURCE_IP'
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 1)
        pool.add()

        # Test with persistence_type = HTTP_COOKIE
        self._mock_BigIp.reset_mock()
        vip.params['persistence_type'] = 'HTTP_COOKIE'
        pool.add()
        self._mock_BigIp.return_value.virtual_server.add_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/http')
        self._mock_BigIp.return_value.virtual_server.set_persist_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/cookie')

        # Test with persistence_type = APP_COOKIE
        self._mock_BigIp.reset_mock()
        vip.params['persistence_type'] = 'APP_COOKIE'
        pool.add()
        self._mock_BigIp.return_value.virtual_server.add_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/http')
        self._mock_BigIp.return_value.virtual_server.set_persist_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/cookie')

        # Test with persistence_type = APP_COOKIE, lb_method = SOURCE_IP
        self._mock_BigIp.reset_mock()
        pool.params['loadbalancer_method'] = 'SOURCE_IP'
        pool.add()
        self._mock_BigIp.return_value.pool.set_lb_method.assert_called_with(
            folder='tenant', lb_method='SOURCE_IP', name='test-lb-pool')
        self._mock_BigIp.return_value.virtual_server.add_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/http')
        self._mock_BigIp.return_value.virtual_server.set_persist_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/cookie')
        self._mock_BigIp.return_value.virtual_server.set_fallback_persist_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/source_addr')

        # Test with persistence_type = APP_COOKIE, lb_method = SOURCE_IP, 
        # persistence_cookie_name = 'DumpKookie'
        self._mock_BigIp.reset_mock()
        vip.params['persistence_cookie_name'] = 'DumpKookie'
        pool.add()
        self._mock_BigIp.return_value.virtual_server.add_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/http')
        self._mock_BigIp.return_value.rule.create.assert_called_with(
            folder='tenant', name='app_cookie_vip', 
            rule_definition='when HTTP_REQUEST {\n if { [HTTP::cookie DumpKookie] ne "" }{\n     persist uie [string tolower [HTTP::cookie "DumpKookie"]] 3600\n }\n}\n\nwhen HTTP_RESPONSE {\n if { [HTTP::cookie "DumpKookie"] ne "" }{\n     persist add uie [string tolower [HTTP::cookie "DumpKookie"]] 3600\n }\n}\n\n')
        self._mock_BigIp.return_value.virtual_server.create_uie_profile.assert_called_with(
            folder='tenant', name='app_cookie_vip', rule_name='app_cookie_vip')
        self._mock_BigIp.return_value.virtual_server.set_persist_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='app_cookie_vip')
        self._mock_BigIp.return_value.virtual_server.set_fallback_persist_profile.assert_called_with(
            folder='tenant', name='vip', profile_name='/Common/source_addr')
        self._mock_BigIp.reset_mock()
        # Cleanup
        for i in range(2):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_vip_persistance_type

    def test_update_hm(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
	hm_obj = self.create_hm_obj("test-hm")
        hm_obj['loadbalancer_pool_back_refs']=[{'uuid': pool.uuid}]
        hm = config_db.HealthMonitorSM.locate(hm_obj['uuid'], hm_obj)
        self.create_pool_members("test-lb-pool", 2)
        pool.add()

        self._mock_BigIp.reset_mock()
        hm_obj['loadbalancer_healthmonitor_properties']['max_retries'] = '100'
        config_db.HealthMonitorSM.update(hm, hm_obj)
        pool.add()
        self._mock_BigIp.return_value.monitor.set_interval.assert_called_with(
            folder='tenant', interval='5', mon_type='HTTP', name='test-hm')
        self._mock_BigIp.return_value.monitor.set_timeout.assert_called_with(
            folder='tenant', mon_type='HTTP', name='test-hm', timeout=200)
        self._mock_BigIp.return_value.monitor.set_send_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          send_text='GET / HTTP/1.0\\r\\n\\r\\n')
        self._mock_BigIp.return_value.monitor.set_recv_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          recv_text='HTTP/1\\.(0|1) 200')

        self._mock_BigIp.reset_mock()
        hm_obj['loadbalancer_healthmonitor_properties']['delay'] = '100'
        config_db.HealthMonitorSM.update(hm, hm_obj)
        pool.add()
        self._mock_BigIp.return_value.monitor.set_interval.assert_called_with(
            folder='tenant', interval='100', mon_type='HTTP', name='test-hm')
        self._mock_BigIp.return_value.monitor.set_timeout.assert_called_with(
            folder='tenant', mon_type='HTTP', name='test-hm', timeout=200)
        self._mock_BigIp.return_value.monitor.set_send_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          send_text='GET / HTTP/1.0\\r\\n\\r\\n')
        self._mock_BigIp.return_value.monitor.set_recv_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          recv_text='HTTP/1\\.(0|1) 200')

        self._mock_BigIp.reset_mock()
        hm_obj['loadbalancer_healthmonitor_properties']['expected_codes'] = '401'
        config_db.HealthMonitorSM.update(hm, hm_obj)
        pool.add()
        self._mock_BigIp.return_value.monitor.set_interval.assert_called_with(
            folder='tenant', interval='100', mon_type='HTTP', name='test-hm')
        self._mock_BigIp.return_value.monitor.set_timeout.assert_called_with(
            folder='tenant', mon_type='HTTP', name='test-hm', timeout=200)
        self._mock_BigIp.return_value.monitor.set_send_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          send_text='GET / HTTP/1.0\\r\\n\\r\\n')
        self._mock_BigIp.return_value.monitor.set_recv_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          recv_text='HTTP/1\\.(0|1) 401')

        self._mock_BigIp.reset_mock()
        hm_obj['loadbalancer_healthmonitor_properties']['timeout'] = '10'
        config_db.HealthMonitorSM.update(hm, hm_obj)
        pool.add()
        self._mock_BigIp.return_value.monitor.set_interval.assert_called_with(
            folder='tenant', interval='100', mon_type='HTTP', name='test-hm')
        self._mock_BigIp.return_value.monitor.set_timeout.assert_called_with(
            folder='tenant', mon_type='HTTP', name='test-hm', timeout=1000)
        self._mock_BigIp.return_value.monitor.set_send_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          send_text='GET / HTTP/1.0\\r\\n\\r\\n')
        self._mock_BigIp.return_value.monitor.set_recv_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          recv_text='HTTP/1\\.(0|1) 401')

        self._mock_BigIp.reset_mock()
        hm_obj['loadbalancer_healthmonitor_properties']['url_path'] = '/status-check'
        config_db.HealthMonitorSM.update(hm, hm_obj)
        pool.add()
        self._mock_BigIp.return_value.monitor.set_interval.assert_called_with(
            folder='tenant', interval='100', mon_type='HTTP', name='test-hm')
        self._mock_BigIp.return_value.monitor.set_timeout.assert_called_with(
            folder='tenant', mon_type='HTTP', name='test-hm', timeout=1000)
        self._mock_BigIp.return_value.monitor.set_send_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          send_text='GET /status-check HTTP/1.0\\r\\n\\r\\n')
        self._mock_BigIp.return_value.monitor.set_recv_string.assert_called_with(
          folder='tenant', mon_type='HTTP', name='test-hm', 
          recv_text='HTTP/1\\.(0|1) 401')

        self._mock_BigIp.reset_mock()
        hm_obj['loadbalancer_healthmonitor_properties']['monitor_type'] = 'PING'
        config_db.HealthMonitorSM.update(hm, hm_obj)
        pool.add()
        self._mock_BigIp.return_value.monitor.set_interval.assert_called_with(
            folder='tenant', interval='100', mon_type='PING', name='test-hm')
        self._mock_BigIp.return_value.monitor.set_timeout.assert_called_with(
            folder='tenant', mon_type='PING', name='test-hm', timeout=1000)

        self._mock_BigIp.reset_mock()
        # Cleanup
        for i in range(2):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
        config_db.HealthMonitorSM.delete('test-hm')
    # end test_add_delete_pool_with_members_vip_hm

    def test_add_delete_multiple_pools(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project)
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        pool.add()

        vip_1 = self.create_vip('vip_1', project)
        pool_1 = self.create_pool("test-lb-pool_1",
               "default-domain:admin:test-lb-pool_1", project, vip_1)
        pool_1.add()

        self.assertEqual(len(self._db), 2)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['f5'].project_list), 1)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['f5'].project_list['tenant']), 2)
        self.assertEqual(self.lb_agent._loadbalancer_driver['f5'].project_list['tenant'], 
            set(['test-lb-pool', 'test-lb-pool_1']))
        
        # Cleanup
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')

        self.assertFalse(self._mock_BigIp.return_value.system.delete_folder.called)

        self.assertEqual(len(self._db), 1)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['f5'].project_list), 1)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['f5'].project_list['tenant']), 1)
        self.assertEqual(self.lb_agent._loadbalancer_driver['f5'].project_list['tenant'], 
            set(['test-lb-pool_1']))
 
        # Cleanup
        config_db.VirtualIpSM.delete('vip_1')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool_1')
        self.assertTrue(self._mock_BigIp.return_value.system.delete_folder.called)

        self.assertEqual(len(self._db), 0)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['f5'].project_list), 0)
    # end test_add_delete_multiple_pools
#end F5LBTest(unittest.TestCase):
