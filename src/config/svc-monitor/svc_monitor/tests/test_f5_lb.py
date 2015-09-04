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
        self.vnc_lib.kv_retrieve.return_value = "fake-pool-vn 40.1.1.0/24"
        self.vnc_lib.service_appliance_set_create.return_value = "opencontrail"

        self._mock_bigip_interfaces = None
        self._mock_BigIp = None

        self._db = {}
        def read_db(id):
            if id in self._db:
                return self._db[id]

        def put_db(id, data):
            self._db[id] = data

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

        def sas_read_side_effect(obj_type, uuids):
            if obj_type == 'service_appliance_set':
                return (True, [{
                 'fq_name': ['default-global-system-config', 'opencontrail'],
                 'service_appliance_driver': 'svc_monitor.services.loadbalancer\
.drivers.ha_proxy.driver.OpencontrailLoadbalancerDriver'
                   }])
            return (False, None)
        DBBase.init(self.svc, None, self.cassandra)
        config_db.ServiceApplianceSetSM._cassandra.read = \
                         mock.Mock(side_effect=sas_read_side_effect)

        # return NoIdError exception for first query
        def no_id_side_effect(fq_name):
            raise NoIdError("xxx")
        self.vnc_lib.service_appliance_set_read = \
            mock.Mock(side_effect=no_id_side_effect)

        self.lb_agent = loadbalancer_agent.LoadbalancerAgent(self.svc, self.vnc_lib,
                                               self.cassandra, self._args)
        self.svc.loadbalancer_agent = self.lb_agent
        sas = config_db.ServiceApplianceSetSM.get('opencontrail')
        self.assertEqual(sas.driver,
"svc_monitor.services.loadbalancer.drivers.ha_proxy.driver.\
OpencontrailLoadbalancerDriver")
        sas.add()
        DBBase.init(self.svc, None, self.cassandra)
        config_db.ServiceApplianceSetSM._cassandra.read = \
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
        sa_obj['service_appliance_user_credentials'] = {'username': "admin", 'password': "contrail123"}
        sa = config_db.ServiceApplianceSM.locate(sa_obj['uuid'], sa_obj)

        def test_decorate_name(name1, name2):
            return name1+'_'+name2

        bigip_patcher = mock.patch('svc_monitor.services.loadbalancer.drivers.f5.f5_driver.f5_bigip.BigIP')
        self._mock_BigIp = bigip_patcher.start()
        self._mock_BigIp.return_value.cluster.get_traffic_groups.return_value = []

        bigip_interface_patcher = mock.patch('svc_monitor.services.loadbalancer.drivers.f5.f5_driver.bigip_interfaces')
        self._mock_bigip_interfaces = bigip_interface_patcher.start()
        self._mock_bigip_interfaces.decorate_name = mock.Mock(side_effect=test_decorate_name)
        sas.add()

    def tearDown(self):
        config_db.ServiceApplianceSetSM.delete("opencontrail")
        config_db.ServiceApplianceSetSM.delete("f5-sas")
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
            {'protocol': 'HTTP', 'subnet_id': 'subnet-id',
             'loadbalancer_method': 'ROUND_ROBIN', 'admin_state': 'true'}
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
                                                    'admin_state': 'true'}
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
        pool_obj.params['admin_state'] = 'false'
    # end update_pool

    def update_vip(self, vip_obj, pool=None):
        vip_obj.params['connection_limit'] = '100'
        vip_obj.params['persistence_type'] = 'always'
        vip_obj.params['admin_state'] = 'false'
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
             'weight': '1', 'status': 'up', 'admin_state': 'true'}
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
                                            'subnet_id': 'subnet_id',
                                            'protocol': 'HTTP',
                                            'admin_state': 'true',
                                            'connection_limit': '-1',
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
        self._pool_si = {}
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
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
        config_db.VirtualIpSM.delete('vip')

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
#end F5LBTest(unittest.TestCase):
