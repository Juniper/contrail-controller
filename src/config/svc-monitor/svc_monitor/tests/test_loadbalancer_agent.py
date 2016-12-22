import mock
from mock import patch
import unittest
from cfgm_common.vnc_db import DBBase
from svc_monitor import config_db
from svc_monitor import loadbalancer_agent
from vnc_api.vnc_api import *
import argparse
import ConfigParser

class LoadbalancerAgentTest(unittest.TestCase):
    def setUp(self):
        self.vnc_lib = mock.Mock()
        self.cassandra = mock.Mock()
        self.logger = mock.Mock()
        self.svc = mock.Mock()
        self._db = {}

        def read_db(id):
            if id in self._db:
                return self._db[id]

        def put_db_config(id, data):
            if id not in self._db:
                self._db[id] = {}
            self._db[id]['config_info'] = data
        def put_db_driver(id, data):
            if id not in self._db:
                self._db[id] = {}
            self._db[id]['driver_info'] = data

        def remove_db(id, data=None):
            if data is None:
                del self._db[id]
                return
            if self._db[id]['driver_info'][data[0]]:
                del self._db[id]['driver_info'][data[0]]

        def list_pools():
            ret_list = []
            for each_entry_id, each_entry_data in self._db.iteritems() or []:
                config_info_obj_dict = each_entry_data['config_info']
                driver_info_obj_dict = None
                if 'driver_info' in each_entry_data:
                    driver_info_obj_dict = each_entry_data['driver_info']
                ret_list.append((each_entry_id, config_info_obj_dict, driver_info_obj_dict))
            return ret_list

        def list_loadbalancers():
            res_list = []
            return res_list

        self.cassandra.loadbalancer_list = mock.Mock(side_effect=list_loadbalancers)
        self.cassandra.pool_list = mock.Mock(side_effect=list_pools)
        self.cassandra.pool_remove = mock.Mock(side_effect=remove_db)
        self.cassandra.pool_driver_info_get = mock.Mock(side_effect=read_db)
        self.cassandra.pool_driver_info_insert = mock.Mock(side_effect=put_db_driver)
        self.cassandra.pool_config_insert = mock.Mock(side_effect=put_db_config)

        mocked_gsc = mock.MagicMock()
        mocked_gsc.uuid = 'fake-gsc-uuid'
        self.vnc_lib.service_appliance_set_create.return_value = "opencontrail"
        self.vnc_lib.global_system_config_read.return_value = mocked_gsc
        def no_id_side_effect(fq_name):
            raise NoIdError("xxx")
        self.vnc_lib.service_appliance_set_read = mock.Mock(side_effect=no_id_side_effect)
        conf_parser = argparse.ArgumentParser(add_help=False)
        config = ConfigParser.SafeConfigParser({'admin_token': None})
        self._args, remaining_argv = conf_parser.parse_known_args()
        self._args.config_sections = config

        def sas_read_side_effect(obj_type, uuids, **kwargs):
            if obj_type == 'service_appliance_set':
                return (True, [{
                   'fq_name': ['default-global-system-config', 'opencontrail'],
                   'service_appliance_driver': 'svc_monitor.services.loadbalancer.drivers.ha_proxy.driver.OpencontrailLoadbalancerDriver'
                   }])
            return (False, None)
        self.cassandra.object_read = mock.Mock(
            side_effect=sas_read_side_effect)
        DBBase.init(self.svc, None, self.cassandra)
        self.lb_agent = loadbalancer_agent.LoadbalancerAgent(self.svc, self.vnc_lib,
                                               self.cassandra, self._args)
        self.svc.loadbalancer_agent = self.lb_agent
        sas = config_db.ServiceApplianceSetSM.get('opencontrail')
        self.assertEqual(sas.driver, "svc_monitor.services.loadbalancer.drivers.ha_proxy.driver.OpencontrailLoadbalancerDriver")
        sas.add()
        self.assertIsNotNone(self.lb_agent._loadbalancer_driver['opencontrail'])
    # end setUp

    def tearDown(self):
        config_db.ServiceApplianceSM.delete("test-lb-provider-0")
        config_db.ServiceApplianceSetSM.delete("test-lb-provider")
        config_db.ServiceApplianceSetSM.delete("opencontrail")
        config_db.LoadbalancerPoolSM.reset()
        config_db.LoadbalancerMemberSM.reset()
        config_db.VirtualIpSM.reset()
    # end tearDown

    def create_sa_set(self, fq_name_str):
        sas_obj = {}
        sas_obj['fq_name'] = fq_name_str.split(':')
        sas_obj['uuid'] = fq_name_str
        sas_obj['display_name'] = fq_name_str
        sas = config_db.ServiceApplianceSetSM.locate(sas_obj['uuid'], sas_obj)
        sas.kvpairs = [{'key': 'key1', 'value': 'value1'},
                       {'key': 'key2', 'value': 'value2'},
                       {'key': 'key3', 'value': 'value3'}]
        sas.ha_mode = "standalone"
        sas.driver = "svc_monitor.tests.fake_lb_driver.OpencontrailFakeLoadbalancerDriver"
        return sas
    # end create_sa_set

    def create_sa(self, fq_name_str, parent_uuid):
        sa_obj = {}
        sa_obj['fq_name'] = fq_name_str.split(':')
        sa_obj['uuid'] = fq_name_str
        sa_obj['display_name'] = fq_name_str
        sa_obj['parent_uuid'] = parent_uuid
        sa_obj['service_appliance_ip_address'] = "1.1.1.1"
        sa_obj['service_appliance_user_credentials'] = {'username': "admin", 'password': "contrail123"}
        sa = config_db.ServiceApplianceSM.locate(sa_obj['uuid'], sa_obj)
        sa.kvpairs = [{'key': 'key1', 'value': 'value1'},
                      {'key': 'key2', 'value': 'value2'},
                      {'key': 'key3', 'value': 'value3'}]
        return sa
    # end create_sa_set

    def create_pool(self, fq_name_str, vip=None, hm=None):
        pool_obj = {}
        pool_obj['loadbalancer_version'] = 'v1'
        pool_obj['fq_name'] = fq_name_str.split(':')
        pool_obj['uuid'] = fq_name_str
        pool_obj['display_name'] = fq_name_str
        pool_obj['parent_uuid'] = 'parent_uuid'
        pool_obj['id_perms'] = {'enable': 'true', 'description': 'Test pool'}
        pool_obj['loadbalancer_pool_provider'] = 'test-lb-provider'
        pool_obj['loadbalancer_pool_properties'] = {'protocol': 'HTTP',
                                                    'subnet_id': 'subnet-id',
                                                    'loadbalancer_method': 'ROUND_ROBIN',
                                                    'session_persistence': None,
                                                    'persistence_cookie_name': None,
                                                    'admin_state': 'true'}
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
        hm_obj['parent_uuid'] = 'parent_uuid'
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
            self.create_pool_member(pool_name, 'member_'+str(i), '10.1.1.'+str(i))
    # end create_pool_members

    def create_pool_member(self, pool_name, member_name, member_address):
        pool_member_obj = {}
        pool_member_obj['fq_name'] = member_name
        pool_member_obj['uuid'] = member_name
        pool_member_obj['display_name'] = member_name
        pool_member_obj['parent_uuid'] = pool_name
        pool_member_obj['id_perms'] = {'enable': 'true', 'description': 'Test pool member'}
        pool_member_obj['loadbalancer_member_properties'] = {'protocol_port': '80',
                                                    'address': member_address,
                                                    'weight': '1',
                                                    'status': 'up',
                                                    'admin_state': 'true'}
        member = config_db.LoadbalancerMemberSM.locate(pool_member_obj['uuid'], pool_member_obj)
    # end create_pool_member

    def create_vip(self, vip):
        vip_obj = {}
        vip_obj['fq_name'] = vip.split(':')
        vip_obj['uuid'] = vip
        vip_obj['display_name'] = vip
        vip_obj['parent_uuid'] = 'parent_uuid'
        vip_obj['id_perms'] = {'enable': 'true', 'description': 'Test pool'}
        vip_obj['virtual_ip_properties'] = {'status': 'UP',
                                            'protocol_port': '80',
                                            'subnet_id': 'subnet_id',
                                            'protocol': 'HTTP',
                                            'admin_state': 'true',
                                            'connection_limit': -1,
                                            'persistence_type': None,
                                            'persistence_cookie_name': None,
                                            'address': '1.1.1.1'}
        vip = config_db.VirtualIpSM.locate(vip_obj['uuid'], vip_obj)
        return vip
    # end create_vip

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

    def validate_hm(self, driver_hm, config_hm):
        self.assertEqual(driver_hm['id'], config_hm.uuid)
        self.assertEqual(driver_hm['tenant_id'], config_hm.parent_uuid)
        self.assertEqual(driver_hm['admin_state_up'], config_hm.params['admin_state'])
        self.assertEqual(driver_hm['delay'], config_hm.params['delay'])
        self.assertEqual(driver_hm['expected_codes'], config_hm.params['expected_codes'])
        self.assertEqual(driver_hm['http_method'], config_hm.params['http_method'])
        self.assertEqual(driver_hm['max_retries'], config_hm.params['max_retries'])
        self.assertEqual(driver_hm['timeout'], config_hm.params['timeout'])
        self.assertEqual(driver_hm['type'], config_hm.params['monitor_type'])
        self.assertEqual(driver_hm['url_path'], config_hm.params['url_path'])
        self.assertEqual(len(driver_hm['pools']), len(config_hm.loadbalancer_pools))
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

    def test_add_delete_sas(self):
        sas = self.create_sa_set("test-lb-provider")
        sa = self.create_sa("test-lb-provider-0", "test-lb-provider")
        sas.add()
        sas_tmp = config_db.ServiceApplianceSetSM.get('test-lb-provider')
        self.assertEqual(sas_tmp.driver, "svc_monitor.tests.fake_lb_driver.OpencontrailFakeLoadbalancerDriver")
        self.assertIsNotNone(self.lb_agent._loadbalancer_driver['test-lb-provider'])
    # end test_add_delete_sas

    def test_add_delete_pool(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        pool = self.create_pool("test-lb-pool")
        pool.add()

        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)

        # Clean up
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_add_delete_pool

    def test_add_delete_hm(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        pool = self.create_pool(fq_name_str="test-lb-pool")
        hm_obj = self.create_hm_obj("test-hm")
        hm_obj['loadbalancer_pool_back_refs']=[{'uuid': pool.uuid}]
        hm = config_db.HealthMonitorSM.locate(hm_obj['uuid'], hm_obj)
        pool.add()

        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        self.validate_hm(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms['test-hm'], hm)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm']), 1)
        self.assertTrue('test-lb-pool' in self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm'])

        # Clean up
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
        config_db.HealthMonitorSM.delete('test-hm')
    # end test_add_delete_hm

    def test_add_delete_pool_with_members(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        pool = self.create_pool("test-lb-pool")
        self.create_pool_members("test-lb-pool", 5)
        pool.add()

        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)

        # Clean up
        for i in range(5):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_add_delete_pool_with_members

    def test_add_delete_pool_with_members_vip(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        vip = self.create_vip('vip')
        pool = self.create_pool("test-lb-pool", vip)
        self.create_pool_members("test-lb-pool", 5)
        pool.add()

        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)
        self.validate_vip(self.lb_agent._loadbalancer_driver['test-lb-provider']._vips['vip'], vip)

        # Cleanup
        for i in range(5):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_add_delete_pool_with_members_vip

    def test_update_pool(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        vip = self.create_vip('vip')
        pool = self.create_pool("test-lb-pool", vip)
        self.create_pool_members("test-lb-pool", 5)
        pool.add()

        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)
        self.validate_vip(self.lb_agent._loadbalancer_driver['test-lb-provider']._vips['vip'], vip)

        # update the Pool
        self.update_pool(pool)
        pool.add()
        # validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)
        self.validate_vip(self.lb_agent._loadbalancer_driver['test-lb-provider']._vips['vip'], vip)

        # Cleanup
        for i in range(5):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_pool

    def test_update_members(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        vip = self.create_vip('vip')
        pool = self.create_pool("test-lb-pool", vip)
        self.create_pool_members("test-lb-pool", 5)
        pool.add()

        # update the Pool-- Add delete even members
        for i in range(5):
            if i%2 == 0:
                config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        pool.add()
        # validate - members
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            if i%2 == 0:
                self.assertIsNone(config_member)
                self.assertIsNone(self.lb_agent._loadbalancer_driver['test-lb-provider']._members.get(id, None))
            else:
                self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)
        # validate - pool
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)

        # Update the pool again.. Add those members back
        for i in range(5):
            if i%2 == 0:
                self.create_pool_member("test-lb-pool", 'member_'+str(i), '22.2.2.'+str(i))
        pool.add()
        # validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        # validate - members
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)

        # Update one member and check
        config_member = config_db.LoadbalancerMemberSM.get('member_3')
        config_member.weight = 20
        pool.add()

        # validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        # validate - members
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)

        # Cleanup
        for i in range(5):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_members

    def test_update_vip(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        vip = self.create_vip('vip')
        pool = self.create_pool("test-lb-pool", vip)
        self.create_pool_members("test-lb-pool", 5)
        pool.add()

        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)
        self.validate_vip(self.lb_agent._loadbalancer_driver['test-lb-provider']._vips['vip'], vip)

        # update the Pool
        self.update_vip(vip)
        pool.add()

        # validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        for i in range(5):
            id = 'member_'+str(i)
            config_member = config_db.LoadbalancerMemberSM.get(id)
            self.validate_pool_member(self.lb_agent._loadbalancer_driver['test-lb-provider']._members[id],  config_member)
        self.validate_vip(self.lb_agent._loadbalancer_driver['test-lb-provider']._vips['vip'], vip)

        # Cleanup
        for i in range(5):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
    # end test_update_vip

    def test_update_hm_props(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        pool = self.create_pool(fq_name_str="test-lb-pool")
        hm_obj = self.create_hm_obj("test-hm")
        hm_obj['loadbalancer_pool_back_refs']=[{'uuid': pool.uuid}]
        hm = config_db.HealthMonitorSM.locate(hm_obj['uuid'], hm_obj)
        pool.add()

        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        self.validate_hm(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms['test-hm'], hm)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm']), 1)
        self.assertTrue('test-lb-pool' in self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm'])

        hm_obj['loadbalancer_healthmonitor_properties']['max_retries'] = '100'
        config_db.HealthMonitorSM.update(hm, hm_obj)
        pool.add()

        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        self.validate_hm(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms['test-hm'], hm)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm']), 1)
        self.assertTrue('test-lb-pool' in self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm'])

        # Clean up
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
        config_db.HealthMonitorSM.delete('test-hm')
    # end test_update_hm_props

    def test_update_hm_pools(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()

        pool = self.create_pool(fq_name_str="test-lb-pool")
        hm_obj = self.create_hm_obj("test-hm")
        hm_obj['loadbalancer_pool_back_refs']=[{'uuid': pool.uuid}]
        hm = config_db.HealthMonitorSM.locate(hm_obj['uuid'], hm_obj)
        pool.add()

        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        self.validate_hm(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms['test-hm'], hm)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm']), 1)
        self.assertTrue('test-lb-pool' in self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm'])

        pool_1 = self.create_pool(fq_name_str="test-lb-pool_1")
        hm_obj['loadbalancer_pool_back_refs']=[{'uuid': pool.uuid}, {'uuid': pool_1.uuid}]
        config_db.HealthMonitorSM.update(hm, hm_obj)
        pool.add()

        # Validate after update
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        self.validate_hm(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms['test-hm'], hm)
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm']), 2)
        self.assertTrue('test-lb-pool' in self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm'])
        self.assertTrue('test-lb-pool_1' in self.lb_agent._loadbalancer_driver['test-lb-provider']._hms_pools['test-hm'])
        # Clean up
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
        config_db.HealthMonitorSM.delete('test-hm')
    # end test_update_hm

    def test_audit_pool(self):
        sas = self.create_sa_set("test-lb-provider")
        sas.add()
        pool = self.create_pool("test-lb-pool")
        pool.add()
        # Validate
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)

        # Delete the pool without letting the driver know about it..
        config_db.LoadbalancerPoolSM.reset()

        # Validate that all pool info is valid in driver..still..
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools), 1)
        self.validate_pool(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools['test-lb-pool'], pool)
        self.assertEqual(len(self._db), 1)
        self.assertTrue('test-lb-pool' in self._db)

        # call audit and ensure pool is deleted
        self.lb_agent.audit_lb_pools()

        # Validate that audit has deleted the pool from driver and from db
        self.assertEqual(len(self.lb_agent._loadbalancer_driver['test-lb-provider']._pools), 0)
        self.assertEqual(len(self._db), 0)
    # end test_audit_pool
#end LoadbalancerAgentTest(unittest.TestCase):
