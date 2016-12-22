import mock
from mock import patch
import unittest
from cfgm_common.vnc_db import DBBase
from svc_monitor import config_db
from svc_monitor import loadbalancer_agent
from vnc_api.vnc_api import *
import argparse
import ConfigParser

class HAProxyTest(unittest.TestCase):
    def setUp(self):
        self.vnc_lib = mock.Mock()
        self.cassandra = mock.Mock()
        self.logger = mock.Mock()
        self.svc = mock.Mock()
        self._si_pool = {}

        mocked_gsc = mock.MagicMock()
        mocked_gsc.uuid = 'fake-gsc-uuid'
        self.vnc_lib.global_system_config_read.return_value = mocked_gsc
        def no_id_side_effect(fq_name):
            raise NoIdError("xxx")
        # Return NoIdError while si is read for first time
        self.vnc_lib.service_instance_read = \
            mock.Mock(side_effect=no_id_side_effect)
        self.vnc_lib.kv_retrieve.return_value = "fake-pool-vn 40.1.1.0/24"
        self.vnc_lib.service_appliance_set_create.return_value = "opencontrail"
        self.vnc_lib.service_appliance_set_read = \
            mock.Mock(side_effect=no_id_side_effect)

        self._store_si = {}
        def read_si(obj_type, uuid, **kwargs):
            return (True, [self.obj_to_dict(self._store_si[uuid[0]])])

        def store_si_create(obj):
            config_db.ServiceInstanceSM._cassandra.object_read = \
                mock.Mock(side_effect=read_si)
            obj.uuid = 'pool-si'
            self._store_si[obj.uuid] = obj


        def update_si_side_effect(obj):
            self._store_si[obj.uuid] = obj

        self.vnc_lib.service_instance_create = \
            mock.Mock(side_effect=store_si_create)

        self.vnc_lib.service_instance_update = \
            mock.Mock(side_effect=update_si_side_effect)

        self._db = {}
        def read_db(id, **kwargs):
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

        def validate_pool_update(obj_type, obj_uuid, ref_type, ref_uuid,
                                 ref_fq_name, operation):
            self.assertEqual(obj_type, "loadbalancer-pool")
            self.assertEqual(ref_type, "service-instance")
            pool = config_db.LoadbalancerPoolSM.get(obj_uuid)
            if operation is "ADD":
                si = config_db.ServiceInstanceSM.get(ref_uuid)
                self.assertIsNotNone(si)
                pool.service_instance = si.uuid
                si.loadbalancer_pool = pool.uuid
                self._si_pool[pool.uuid] = si.uuid
                self.assertEqual(si.uuid, "pool-si")
            elif operation is "DELETE":
                pool.service_instance = None
                del self._si_pool[pool.uuid]
            else:
                self.assertTrue(False)
            return
        self.vnc_lib.ref_update = mock.Mock(side_effect=validate_pool_update)

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

        self.lb_agent = loadbalancer_agent.LoadbalancerAgent(self.svc, self.vnc_lib,
                                               self.cassandra, self._args)
        self.svc.loadbalancer_agent = self.lb_agent
        sas = config_db.ServiceApplianceSetSM.get('opencontrail')
        self.assertEqual(sas.driver,
"svc_monitor.services.loadbalancer.drivers.ha_proxy.driver.\
OpencontrailLoadbalancerDriver")
        sas.add()
        self.assertIsNotNone(self.lb_agent._loadbalancer_driver['opencontrail'])
        mock_st_obj = self.create_lb_st()
    # end setUp

    def create_lb_st(self):
        domain_name = 'default-domain'
        domain_fq_name = [domain_name]
        domain_obj = Domain()
        domain_obj.uuid = 'fake-domain'
        domain_obj.fq_name = domain_fq_name

        svc_properties = ServiceTemplateType()
        svc_properties.set_service_type("loadbalancer")
        svc_properties.set_service_mode("in-network-nat")
        svc_properties.set_service_virtualization_type("network-namespace")
        svc_properties.set_image_name(None)
        svc_properties.set_flavor(None)
        svc_properties.set_ordered_interfaces(True)
        svc_properties.set_service_scaling(True)

        # set interface list
        if_list = [['right', True], ['left', True]]
        for itf in if_list:
            if_type = ServiceTemplateInterfaceType(shared_ip=itf[1])
            if_type.set_service_interface_type(itf[0])
            svc_properties.add_interface_type(if_type)

        st_obj = ServiceTemplate(name="haproxy-loadbalancer-template",
                                 domain_obj=domain_obj)
        st_obj.set_service_template_properties(svc_properties)
        st_obj.uuid = 'haproxy-st'
        st_dict = self.obj_to_dict(st_obj)
        st_uuid = config_db.ServiceTemplateSM.locate(st_obj.uuid, st_dict)
        return st_obj
    # end

    def tearDown(self):
        config_db.ServiceApplianceSetSM.delete("opencontrail")
        config_db.ServiceTemplateSM.delete('haproxy-st')
        config_db.LoadbalancerPoolSM.reset()
        config_db.VirtualIpSM.reset()
        config_db.InstanceIpSM.reset()
        config_db.VirtualMachineInterfaceSM.reset()
        config_db.VirtualNetworkSM.reset()
        config_db.ProjectSM.reset()
        del self._store_si
    # end tearDown

    def create_pool(self, uuid, fq_name_str, project=None, vip=None, hm=None):
        pool_network = self.create_vn("fake-pool-vn", "fake-pool-vn", project)
        pool_obj = {}
        pool_obj['fq_name'] = fq_name_str.split(':')
        pool_obj['uuid'] = uuid
        pool_obj['display_name'] = fq_name_str
        pool_obj['parent_uuid'] = 'parent_uuid'
        pool_obj['id_perms'] = {'enable': 'true', 'description': 'Test pool'}
        pool_obj['loadbalancer_pool_provider'] = 'opencontrail'
        pool_obj['loadbalancer_pool_properties'] = \
            {'protocol': 'HTTP', 'subnet_id': 'subnet-id',
             'loadbalancer_method': 'ROUND_ROBIN', 'admin_state': 'true',
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

    def create_vip(self, vip, project, vn, vmi, ip_addr):
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
                                            'connection_limit': -1,
                                            'persistence_type': None,
                                            'persistence_cookie_name': None,
                                            'address': ip_addr}
        network = self.create_vn(vn, vn, project)
        vmi = self.create_vmi(vmi, vmi, project, network)
        iip = self.create_iip(ip_addr, ip_addr, ip_addr, network, vmi)
        vip_vnc = VirtualIp.from_dict(**vip_obj)
        vip_vnc.set_virtual_machine_interface(vmi)
        vip_obj = self.obj_to_dict(vip_vnc)
        vip_obj['parent_uuid'] = project.uuid
        vip = config_db.VirtualIpSM.locate(vip, vip_obj)
        return vip
    # end create_vip

    def test_add_delete_pool_with_members_vip(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project, 'fake-vip-vn', 'vmi', '1.1.1.1')
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 5)
        pool.add()
        self.assertEqual(len(self._db), 1)
        self.assertTrue('test-lb-pool' in self._db)
        self.assertEqual(self._db['test-lb-pool']['service_instance'],
                         'pool-si')
        self.assertEqual(len(self._si_pool), 1)
        si_uuid = self._si_pool['test-lb-pool']
        self.assertEqual(si_uuid, 'pool-si')

        si = config_db.ServiceInstanceSM.get(si_uuid)
        self.assertEqual(si.service_template, 'haproxy-st')
        self.assertEqual(si.params['scale_out']['max_instances'], 2)
        self.assertEqual(si.params['scale_out']['auto_scale'], False)
        self.assertEqual(si.params['ha_mode'], 'active-standby')
        self.assertEqual(si.params['interface_list'][0]['ip_address'],
                         '1.1.1.1')
        self.assertEqual(si.params['interface_list'][0]['virtual_network'],
                         'default-domain:fake-project:fake-vip-vn')
        self.assertEqual(si.params['interface_list'][1]['ip_address'], None)
        self.assertEqual(si.params['interface_list'][1]['virtual_network'],
                         'default-domain:fake-project:fake-pool-vn')

        # Cleanup
        for i in range(5):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
        self.assertEqual(len(self._si_pool), 0)
        self.assertEqual(len(config_db.ServiceInstanceSM._dict.keys()), 0)
    # end test_add_delete_pool_with_members_vip
    #
    # In this test, update the vip on the pool
    # Create a pool and vip
    # Create a new vip and link it to the pool
    # Expected result is the service instance is updated with new interface list
    #
    def test_update_vip(self):
        project = self.create_project("fake-project", "project")
        vip = self.create_vip('vip', project, 'fake-vip-vn', 'vmi', '1.1.1.1')
        pool = self.create_pool("test-lb-pool",
               "default-domain:admin:test-lb-pool", project, vip)
        self.create_pool_members("test-lb-pool", 5)
        pool.add()
        self.assertEqual(len(self._db), 1)
        self.assertTrue('test-lb-pool' in self._db)
        self.assertEqual(self._db['test-lb-pool']['service_instance'],
                         'pool-si')
        self.assertEqual(len(self._si_pool), 1)
        si_uuid = self._si_pool['test-lb-pool']
        self.assertEqual(si_uuid, 'pool-si')

        si = config_db.ServiceInstanceSM.get(si_uuid)
        self.assertEqual(si.service_template, 'haproxy-st')
        self.assertEqual(si.params['scale_out']['max_instances'], 2)
        self.assertEqual(si.params['scale_out']['auto_scale'], False)
        self.assertEqual(si.params['ha_mode'], 'active-standby')
        self.assertEqual(si.params['interface_list'][0]['ip_address'],
                         '1.1.1.1')
        self.assertEqual(si.params['interface_list'][0]['virtual_network'],
                         'default-domain:fake-project:fake-vip-vn')
        self.assertEqual(si.params['interface_list'][1]['ip_address'], None)
        self.assertEqual(si.params['interface_list'][1]['virtual_network'],
                         'default-domain:fake-project:fake-pool-vn')

        # Create a new vip
        vip_new = self.create_vip('vip-new', project, 'fake-vip-vn-new', 'vmi-new', '99.1.1.1')
        pool = config_db.LoadbalancerPoolSM.get('test-lb-pool')
        # Link it to the pool created before
        pool.virtual_ip = vip_new.uuid
        vip_new.loadbalancer_pool = pool.uuid

        def read_si_side_effect(id):
            return self._store_si[id]
        # Return the stored SI data
        self.vnc_lib.service_instance_read = \
            mock.Mock(side_effect=read_si_side_effect)

        pool.add()
        self.assertEqual(len(self._db), 1)
        self.assertTrue('test-lb-pool' in self._db)
        self.assertEqual(self._db['test-lb-pool']['service_instance'],
                         'pool-si')
        self.assertEqual(len(self._si_pool), 1)
        si_uuid = self._si_pool['test-lb-pool']
        self.assertEqual(si_uuid, 'pool-si')

        si = config_db.ServiceInstanceSM.get(si_uuid)
        self.assertEqual(si.params['scale_out']['max_instances'], 2)
        self.assertEqual(si.params['scale_out']['auto_scale'], False)
        self.assertEqual(si.params['ha_mode'], 'active-standby')
        self.assertEqual(si.params['interface_list'][0]['ip_address'],
                         '99.1.1.1')
        self.assertEqual(si.params['interface_list'][0]['virtual_network'],
                         'default-domain:fake-project:fake-vip-vn-new')
        self.assertEqual(si.params['interface_list'][1]['ip_address'], None)
        self.assertEqual(si.params['interface_list'][1]['virtual_network'],
                         'default-domain:fake-project:fake-pool-vn')
        # Cleanup
        for i in range(5):
            config_db.LoadbalancerMemberSM.delete('member_'+str(i))
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('test-lb-pool')
        self.assertEqual(len(self._si_pool), 0)
        self.assertEqual(len(config_db.ServiceInstanceSM._dict.keys()), 0)
    # end test_update_vip
#end HAProxyTest(unittest.TestCase):
