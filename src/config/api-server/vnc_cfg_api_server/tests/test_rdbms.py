#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
sys.path.append('../common/tests')

import os
import shutil

import test_common
import sqlalchemy
import testtools
import test_case

import keystoneclient.exceptions as kc_exceptions
import keystoneclient.v2_0.client as keystone
from keystonemiddleware import auth_token

from vnc_api.vnc_api import *
from test_utils import *
from cfgm_common import vnc_rdbms
from testtools.matchers import Equals, MismatchError, Not, Contains, LessThan
from cfgm_common.exceptions import ResourceExhaustionError, ResourceExistsError
from testtools import content, content_type, ExpectedException

import test_utils
import test_crud_basic
import test_subnet_ip_count
import test_perms2
import test_logical_router
import test_ip_alloc
import test_fc_id
import test_askip

class TestFixtures(test_crud_basic.TestFixtures, test_case.ApiServerRDBMSTestCase):
    pass

class TestListUpdateS(test_crud_basic.TestListUpdate, test_case.ApiServerRDBMSTestCase):
    pass

class TestCrud(test_crud_basic.TestCrud, test_case.ApiServerRDBMSTestCase):
    pass

class TestBulk(test_crud_basic.TestBulk, test_case.ApiServerRDBMSTestCase):
    pass

class TestVncCfgApiServer(
        test_crud_basic.TestVncCfgApiServer, test_case.ApiServerRDBMSTestCase):
    def test_ip_addr_not_released_on_delete_error(self):
        ipam_obj = NetworkIpam('ipam-%s' %(self.id()))
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        vn_obj.add_network_ipam(ipam_obj,
            VnSubnetsType(
                [IpamSubnetType(SubnetType('1.1.1.0', 28))]))
        self._vnc_lib.virtual_network_create(vn_obj)

        # instance-ip test
        iip_obj = InstanceIp('iip-%s' %(self.id()))
        iip_obj.add_virtual_network(vn_obj)
        self._vnc_lib.instance_ip_create(iip_obj)
        # read back to get allocated ip
        iip_obj = self._vnc_lib.instance_ip_read(id=iip_obj.uuid)

        def err_on_delete(orig_method, *args, **kwargs):
            if args[0] == 'instance_ip':
                raise Exception("Faking db delete for instance ip")
            return orig_method(*args, **kwargs)
        with test_common.patch(
            self._api_server._db_conn, 'dbe_delete', err_on_delete):
            try:
                self._vnc_lib.instance_ip_delete(id=iip_obj.uuid)
                self.assertTrue(
                    False, 'Instance IP delete worked unexpectedly')
            except Exception as e:
                self.assertThat(str(e),
                    Contains('"Faking db delete for instance ip"'))

        # floating-ip test
        fip_pool_obj = FloatingIpPool(
            'fip-pool-%s' %(self.id()), parent_obj=vn_obj)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        fip_obj = FloatingIp('fip-%s' %(self.id()), parent_obj=fip_pool_obj)
        fip_obj.add_project(Project())
        self._vnc_lib.floating_ip_create(fip_obj)
        # read back to get allocated floating-ip
        fip_obj = self._vnc_lib.floating_ip_read(id=fip_obj.uuid)

        def err_on_delete(orig_method, *args, **kwargs):
            if args[0] == 'floating_ip':
                raise Exception("Faking db delete for floating ip")
            if args[0] == 'alias_ip':
                raise Exception("Faking db delete for alias ip")
            return orig_method(*args, **kwargs)
        with test_common.patch(
            self._api_server._db_conn, 'dbe_delete', err_on_delete):
            try:
                self._vnc_lib.floating_ip_delete(id=fip_obj.uuid)
                self.assertTrue(
                    False, 'Floating IP delete worked unexpectedly')
            except Exception as e:
                self.assertThat(str(e),
                    Contains('"Faking db delete for floating ip"'))

        # alias-ip test
        aip_pool_obj = AliasIpPool(
            'aip-pool-%s' %(self.id()), parent_obj=vn_obj)
        self._vnc_lib.alias_ip_pool_create(aip_pool_obj)
        aip_obj = AliasIp('aip-%s' %(self.id()), parent_obj=aip_pool_obj)
        aip_obj.add_project(Project())
        self._vnc_lib.alias_ip_create(aip_obj)
        # read back to get allocated alias-ip
        aip_obj = self._vnc_lib.alias_ip_read(id=aip_obj.uuid)

        with test_common.patch(
            self._api_server._db_conn, 'dbe_delete', err_on_delete):
            try:
                self._vnc_lib.alias_ip_delete(id=aip_obj.uuid)
                self.assertTrue(
                    False, 'Alias IP delete worked unexpectedly')
            except Exception as e:
                self.assertThat(str(e),
                    Contains('"Faking db delete for alias ip"'))
    #end test_ip_addr_not_released_on_delete_error


class TestVncCfgApiServerRequests(test_crud_basic.TestVncCfgApiServerRequests, test_case.ApiServerRDBMSTestCase):
    pass


class TestLocalAuth(test_crud_basic.TestLocalAuth, test_case.ApiServerRDBMSTestCase):
    pass


class TestExtensionApi(test_crud_basic.TestExtensionApi, test_case.ApiServerRDBMSTestCase):
    pass


class TestPropertyWithList(test_crud_basic.TestPropertyWithList, test_case.ApiServerRDBMSTestCase):
    def test_set_in_object(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())
        vmi_obj.set_virtual_machine_interface_fat_flow_protocols(
            FatFlowProtocols([ProtocolType(protocol='p1', port=1),
                              ProtocolType(protocol='p2', port=2)]))
        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        # ensure stored as list order
        rd_vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)
        rd_ff_proto = rd_vmi_obj.virtual_machine_interface_fat_flow_protocols
        self.assertThat(
            rd_ff_proto.fat_flow_protocol[0].protocol, Equals('p1'))
        self.assertThat(
            rd_ff_proto.fat_flow_protocol[1].protocol, Equals('p2'))

        vmi_obj.set_virtual_machine_interface_fat_flow_protocols(
            FatFlowProtocols())
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        rd_vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)
        rd_ff_proto = rd_vmi_obj.virtual_machine_interface_fat_flow_protocols
        self.assertIsNone(rd_ff_proto)

    def test_add_del_in_object(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())

        for proto,port,pos in [('proto1', 1, 0), ('proto2', 2, 1),
                            ('proto3', 3, 2), ('proto4', 4, None)]:
            vmi_obj.add_virtual_machine_interface_fat_flow_protocols(
                ProtocolType(protocol=proto, port=port), pos)

        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        rd_ff_proto = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid).virtual_machine_interface_fat_flow_protocols

        self.assertEqual(len(rd_ff_proto.fat_flow_protocol), 4)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[0].protocol, 'proto1')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[0].port, 1)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[1].protocol, 'proto2')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[1].port, 2)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[2].protocol, 'proto3')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[2].port, 3)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[3].protocol, 'proto4')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[3].port, 4)

        for pos in ['0', '2']:
            vmi_obj.del_virtual_machine_interface_fat_flow_protocols(
                elem_position=pos)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        rd_ff_proto = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid).virtual_machine_interface_fat_flow_protocols

        self.assertEqual(len(rd_ff_proto.fat_flow_protocol), 2)

        self.assertEqual(rd_ff_proto.fat_flow_protocol[0].protocol, 'proto2')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[0].port, 2)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[1].protocol, 'proto4')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[1].port, 4)
    # end test_add_del_in_object

    def test_prop_list_add_delete_get_element(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        # 1. Add tests
        # add with element as type
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            ProtocolType('proto1', 0))

        # add with element as dict
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            {'protocol':'proto2', 'port': 1})

        # verify above add without position specified generated uuid'd order
        rd_ff_proto = self._vnc_lib.prop_list_get(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols')
        self.assertEqual(len(rd_ff_proto), 2)

        # add with position specified
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            {'protocol':'proto3', 'port':2}, '0')
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            {'protocol':'proto4', 'port':3}, '1')
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            {'protocol':'proto5', 'port':4}, '2')

        # 2. Get tests (specific and all elements)
        # get specific element
        rd_ff_proto = self._vnc_lib.prop_list_get(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols', '1')
        self.assertEqual(len(rd_ff_proto), 1)
        self.assert_kvpos(rd_ff_proto, 0, 'proto4', 3, 0)

        # get all elements
        rd_ff_proto = self._vnc_lib.prop_list_get(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols')

        self.assertEqual(len(rd_ff_proto), 3)

        self.assert_kvpos(rd_ff_proto, 0, 'proto3', 2, 0)
        self.assert_kvpos(rd_ff_proto, 1, 'proto4', 3, 1)
        self.assert_kvpos(rd_ff_proto, 2, 'proto5', 4, 2)

        # 3. Delete tests
        self._vnc_lib.prop_list_delete_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols', 0)
        self._vnc_lib.prop_list_delete_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols', 1)
        self._vnc_lib.prop_list_delete_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols', 2)

        rd_ff_proto = self._vnc_lib.prop_list_get(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols')
        self.assertEqual(len(rd_ff_proto), 0)

    def test_set_in_resource_body_rest_api(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        url = 'http://%s:%s/virtual-machine-interfaces' %(
            listen_ip, listen_port)
        vmi_body = {
            'virtual-machine-interface': {
                'fq_name': ['default-domain',
                            'default-project',
                            'vmi-%s' %(self.id())],
                'parent_type': 'project',
                'virtual_machine_interface_fat_flow_protocols': {
                    'fat_flow_protocol': [
                        {'protocol': 'proto1', 'port': 1},
                        {'protocol': 'proto1', 'port': 2},
                        {'protocol': 'proto2', 'port': 1},
                        {'protocol': 'proto2', 'port': 2},
                    ]
                },
                'virtual_network_refs': [
                    {'to': ['default-domain',
                            'default-project',
                            'default-virtual-network']}
                ]
            }
        }

        vmi_resp = requests.post(url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(vmi_body))
        vmi_uuid = json.loads(
            vmi_resp.content)['virtual-machine-interface']['uuid']
        vmi_url = 'http://%s:%s/virtual-machine-interface/%s' %(
            listen_ip, listen_port, vmi_uuid)

        vmi_read = json.loads(
            requests.get(vmi_url).content)['virtual-machine-interface']
        rd_ff_proto = vmi_read['virtual_machine_interface_fat_flow_protocols']
        self.assertEqual(len(rd_ff_proto['fat_flow_protocol']), 4)
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][0]['protocol'], 'proto1')
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][1]['protocol'], 'proto1')
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][2]['protocol'], 'proto2')
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][3]['protocol'], 'proto2')

        vmi_body = {
            'virtual-machine-interface': {
                'virtual_machine_interface_fat_flow_protocols': {
                    'fat_flow_protocol': [
                        {'protocol': 'proto3', 'port': 3}
                    ]
                }
            }
        }
        vmi_resp = requests.put(vmi_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(vmi_body))

        vmi_read = json.loads(
            requests.get(vmi_url).content)['virtual-machine-interface']
        rd_ff_proto = vmi_read['virtual_machine_interface_fat_flow_protocols']
        self.assertEqual(len(rd_ff_proto['fat_flow_protocol']), 1)
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][0]['protocol'], 'proto3')
    # end test_set_in_resource_body_rest_api

    def _rest_vmi_create(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        url = 'http://%s:%s/virtual-machine-interfaces' %(
            listen_ip, listen_port)
        vmi_body = {
            'virtual-machine-interface': {
                'fq_name': ['default-domain',
                            'default-project',
                            'vmi-%s' %(self.id())],
                'parent_type': 'project',
                'virtual_network_refs': [
                    {'to': ['default-domain',
                            'default-project',
                            'default-virtual-network']}
                ]
            }
        }

        vmi_resp = requests.post(url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(vmi_body))
        vmi_uuid = json.loads(
            vmi_resp.content)['virtual-machine-interface']['uuid']

        return vmi_uuid
    # end _rest_vmi_create

    def test_prop_list_add_delete_get_rest_api(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        vmi_uuid = self._rest_vmi_create()

        prop_coll_update_url = 'http://%s:%s/prop-collection-update' %(
            listen_ip, listen_port)
        prop_coll_get_url = 'http://%s:%s/prop-collection-get' %(
            listen_ip, listen_port)
        # 1. Add elements
        requests.post(prop_coll_update_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(
                {'uuid': vmi_uuid,
                 'updates': [
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'add',
                      'value': {'protocol': 'proto1', 'port': 1} },
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'add',
                      'value': {'protocol': 'proto2', 'port': 2},
                      'position': 1},
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'add',
                      'value': {'protocol': 'proto3', 'port': 3}} ] }))

        # 2. Get elements (all and specific)
        # get all elements
        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['virtual_machine_interface_fat_flow_protocols'])}
        rd_ff_proto = json.loads(requests.get(prop_coll_get_url,
            params=query_params).content)['virtual_machine_interface_fat_flow_protocols']

        self.assertEqual(len(rd_ff_proto), 3)
        self.assertEqual(rd_ff_proto[0][0]['protocol'], 'proto1')
        self.assertEqual(rd_ff_proto[0][0]['port'], 1)
        self.assertEqual(rd_ff_proto[0][1], 0)

        self.assertEqual(rd_ff_proto[2][0]['protocol'], 'proto3')
        self.assertEqual(rd_ff_proto[2][0]['port'], 3)
        self.assertEqual(rd_ff_proto[2][1], 2)

        # get specific element
        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['virtual_machine_interface_fat_flow_protocols']),
                        'position': 2}
        rd_ff_proto = json.loads(requests.get(prop_coll_get_url,
            params=query_params).content)['virtual_machine_interface_fat_flow_protocols']
        self.assertEqual(len(rd_ff_proto), 1)
        self.assertEqual(rd_ff_proto[0][0]['protocol'], 'proto3')
        self.assertEqual(rd_ff_proto[0][0]['port'], 3)
        self.assertEqual(rd_ff_proto[0][1], 0)

        # 3. Modify specific elements
        requests.post(prop_coll_update_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(
                {'uuid': vmi_uuid,
                 'updates': [
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'modify',
                      'value': {'protocol': 'proto2', 'port': 21},
                      'position': 1},
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'modify',
                      'value': {'protocol': 'proto3', 'port': 31},
                      'position': 2} ] }))

        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['virtual_machine_interface_fat_flow_protocols'])}
        rd_ff_proto = json.loads(requests.get(prop_coll_get_url,
            params=query_params).content)['virtual_machine_interface_fat_flow_protocols']

        self.assertEqual(len(rd_ff_proto), 3)

        self.assertEqual(rd_ff_proto[1][0]['protocol'], 'proto2')
        self.assertEqual(rd_ff_proto[1][0]['port'], 21)
        self.assertEqual(rd_ff_proto[1][1], 1)

        self.assertEqual(rd_ff_proto[2][0]['protocol'], 'proto3')
        self.assertEqual(rd_ff_proto[2][0]['port'], 31)
        self.assertEqual(rd_ff_proto[2][1], 2)

        # 4. Delete (and add) elements
        requests.post(prop_coll_update_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(
                {'uuid': vmi_uuid,
                 'updates': [
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'delete',
                      'position': 0},
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'delete',
                      'position': 1},
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'add',
                      'value': {'protocol': 'proto4', 'port': 4},
                      'position': 1} ] }))

        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['virtual_machine_interface_fat_flow_protocols'])}
        rd_ff_proto = json.loads(requests.get(prop_coll_get_url,
            params=query_params).content)['virtual_machine_interface_fat_flow_protocols']

        self.assertEqual(len(rd_ff_proto), 2)
        self.assertEqual(rd_ff_proto[0][0]['protocol'], 'proto4')
        self.assertEqual(rd_ff_proto[0][0]['port'], 4)
        self.assertEqual(rd_ff_proto[1][0]['protocol'], 'proto3')
        self.assertEqual(rd_ff_proto[1][0]['port'], 31)
        self.assertEqual(rd_ff_proto[1][1], 1)
    # end test_prop_list_add_delete_get_rest_api

    def test_prop_list_wrong_type_should_fail(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        vmi_uuid = self._rest_vmi_create()

        prop_coll_update_url = 'http://%s:%s/prop-collection-update' %(
            listen_ip, listen_port)
        prop_coll_get_url = 'http://%s:%s/prop-collection-get' %(
            listen_ip, listen_port)

        # 1. Try adding elements to non-prop-list field
        response = requests.post(prop_coll_update_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(
                {'uuid': vmi_uuid,
                 'updates': [
                     {'field': 'display_name',
                      'operation': 'add',
                      'value': {'key': 'k3', 'value': 'v3'},
                      'position': 1} ] }))
        self.assertEqual(response.status_code, 400)

        # 2. Try getting elements from non-prop-list field
        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['display_name'])}
        response = requests.get(prop_coll_get_url,
            params=query_params)
        self.assertEqual(response.status_code, 400)
    # end test_prop_list_wrong_type_should_fail

    def test_resource_list_with_field_prop_list(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' % (self.id()),
                                          parent_obj=Project())
        fname = 'virtual_machine_interface_fat_flow_protocols'
        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        vmis = self._vnc_lib.virtual_machine_interfaces_list(
            obj_uuids=[vmi_obj.uuid], fields=[fname])
        vmi_ids = [vmi['uuid'] for vmi in vmis['virtual-machine-interfaces']]
        self.assertEqual([vmi_obj.uuid], vmi_ids)
        self.assertNotIn(fname, vmis['virtual-machine-interfaces'][0])

        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_obj.uuid)
        proto_type = ProtocolType(protocol='proto', port=1)
        vmi_obj.add_virtual_machine_interface_fat_flow_protocols(proto_type,
                                                                 1)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        vmis = self._vnc_lib.virtual_machine_interfaces_list(
            obj_uuids=[vmi_obj.uuid], fields=[fname])
        vmi_ids = [vmi['uuid'] for vmi in vmis['virtual-machine-interfaces']]
        self.assertEqual([vmi_obj.uuid], vmi_ids)
        self.assertIn(fname, vmis['virtual-machine-interfaces'][0])
        self.assertDictEqual({'fat_flow_protocol': [vars(proto_type)]},
                             vmis['virtual-machine-interfaces'][0][fname])
# end class TestPropertyWithlist

class TestPropertyWithMap(test_crud_basic.TestPropertyWithMap, test_case.ApiServerRDBMSTestCase):
    _excluded_vmi_bindings = ['vif_type', 'vnic_type']

    def test_set_in_object(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())
        vmi_obj.set_virtual_machine_interface_bindings(
            KeyValuePairs([KeyValuePair(key='k1', value='v1'),
                           KeyValuePair(key='k2', value='v2')]))
        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        # ensure stored as list order
        vmi = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)
        rd_bindings = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid).virtual_machine_interface_bindings
        bindings_dict = {binding.key: binding.value for binding in
                         rd_bindings.key_value_pair
                         if binding.key not in self._excluded_vmi_bindings}
        self.assertDictEqual(bindings_dict, {'k1': 'v1', 'k2': 'v2'})

        # update and clobber old entries
        #vmi_obj.set_virtual_machine_interface_bindings([])
        vmi_obj.set_virtual_machine_interface_bindings(KeyValuePairs())
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        rd_vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)
        rd_bindings = rd_vmi_obj.virtual_machine_interface_bindings
        self.assertIsNone(rd_bindings)
    # end test_set_in_object


class TestRDBMSIndexAllocator(testtools.TestCase):
    def setUp(self):
        super(TestRDBMSIndexAllocator, self).setUp()
        shutil.copyfile('./base_db.db', "test_id_allocater.db")
        connection = "sqlite:///test_id_allocater.db"
        engine_args = {
            'echo': False,
        }
        engine = sqlalchemy.create_engine(connection, **engine_args)
        #vnc_rdbms.Base.metadata.create_all(engine)
        self.db = engine
        self.Session = sqlalchemy.orm.sessionmaker(bind=engine)

    def test_init(self):
        path = "/test"
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 100, start_idx=0, reverse=False,
            alloc_list=None, max_alloc=0)
        session = self.Session()
        allocations = session.query(vnc_rdbms.IDPool).all()
        self.assertEqual(1, len(allocations))

    def test_init_with_allocation(self):
        path = "/test"
        alloc_list = [
            {"start": 100, "end": 200},
            {"start": 300, "end": 400},
            {"start": 10, "end": 20},
            {"start": 10000000, "end": 340282366920938463463374607431768211455}
        ]
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 100, start_idx=0, reverse=False,
            alloc_list=alloc_list, max_alloc=0)
        session = self.Session()
        allocations = session.query(vnc_rdbms.IDPool).all()
        self.assertEqual(4, len(allocations))

    def test_init_with_overlap_allocation(self):
        path = "/test"

        with testtools.ExpectedException(Exception) as e:
            alloc_list = [
                {"start": 100, "end": 200},
                {"start": 10, "end": 400}
            ]
            vnc_rdbms.RDBMSIndexAllocator(
                self.db, path, 100, start_idx=0, reverse=False,
                alloc_list=alloc_list, max_alloc=0)

        with testtools.ExpectedException(Exception) as e:
            alloc_list = [
                {"start": 10, "end": 400},
                {"start": 100, "end": 200}
            ]
            vnc_rdbms.RDBMSIndexAllocator(
                self.db, path, 100, start_idx=0, reverse=False,
                alloc_list=alloc_list, max_alloc=0)

        with testtools.ExpectedException(Exception) as e:
            alloc_list = [
                {"start": 100, "end": 200},
                {"start": 150, "end": 400}
            ]
            vnc_rdbms.RDBMSIndexAllocator(
                self.db, path, 100, start_idx=0, reverse=False,
                alloc_list=alloc_list, max_alloc=0)

        with testtools.ExpectedException(Exception) as e:
            alloc_list = [
                {"start": 150, "end": 400},
                {"start": 100, "end": 200}
            ]
            vnc_rdbms.RDBMSIndexAllocator(
                self.db, path, 100, start_idx=0, reverse=False,
                alloc_list=alloc_list, max_alloc=0)

        with testtools.ExpectedException(Exception) as e:
            alloc_list = [
                {"start": 20000000, "end": 30000000},
                {"start": 10000000, "end": 340282366920938463463374607431768211455}
            ]
            vnc_rdbms.RDBMSIndexAllocator(
                self.db, path, 100, start_idx=0, reverse=False,
                alloc_list=alloc_list, max_alloc=0)

    def test_set_in_use(self):
        path = "/test"
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 100, start_idx=0)
        session = self.Session()
        allocator.set_in_use(50)

        allocations = session.query(vnc_rdbms.IDPool).all()
        self.assertEqual(3, len(allocations))
        self.assertEqual(1, allocator.get_alloc_count())

    def test_reset_in_use(self):
        path = "/test"
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 100, start_idx=0)
        session = self.Session()
        allocator.set_in_use(50)
        allocator.reset_in_use(50)
        allocations = session.query(vnc_rdbms.IDPool).filter(vnc_rdbms.IDPool.used == False ).all()
        self.assertEqual(3, len(allocations))
        self.assertEqual(0, allocator.get_alloc_count())

    def test_alloc(self):
        path = "/test"
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 100, start_idx=0)
        session = self.Session()
        allocations = session.query(vnc_rdbms.IDPool).all()
        id1 = allocator.alloc("alloc1")
        self.assertEqual(0, id1)
        id2 = allocator.alloc("alloc2")
        self.assertEqual(1, id2)

        self.assertEqual('alloc1', allocator.read(id1))

        allocations = session.query(vnc_rdbms.IDPool).all()
        self.assertEqual(2, allocator.get_alloc_count())

    def test_alloc_reverse(self):
        path = "/test"
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 100, start_idx=0, reverse=True)
        session = self.Session()
        allocations = session.query(vnc_rdbms.IDPool).all()
        id1 = allocator.alloc("alloc1")
        self.assertEqual(99, id1)
        id2 = allocator.alloc("alloc2")
        self.assertEqual(98, id2)
        allocations = session.query(vnc_rdbms.IDPool).all()
        self.assertEqual(2, allocator.get_alloc_count())

    def test_alloc_exhausted(self):
        path = "/test"
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 2, start_idx=0)
        session = self.Session()
        allocator.alloc("alloc1")
        allocator.alloc("alloc2")
        with testtools.ExpectedException(ResourceExhaustionError) as e:
            allocator.alloc("alloc3")

        self.assertEqual(2, allocator.get_alloc_count())
        self.assertEqual(False, allocator.empty())

    def test_alloc_exhausted_reverse(self):
        path = "/test"
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 2, start_idx=0, reverse=True)
        session = self.Session()
        allocator.alloc("alloc1")
        allocator.alloc("alloc2")
        with testtools.ExpectedException(ResourceExhaustionError) as e:
            allocator.alloc("alloc3")

        self.assertEqual(2, allocator.get_alloc_count())

    def test_delete_all(self):
        path = "/test"
        allocator = vnc_rdbms.RDBMSIndexAllocator(
            self.db, path, 2, start_idx=0)
        session = self.Session()
        allocator.alloc("alloc1")
        allocator.alloc("alloc2")
        vnc_rdbms.RDBMSIndexAllocator.delete_all(self.db, path)
        allocations = session.query(vnc_rdbms.IDPool).all()
        self.assertEqual(0, len(allocations))

class TestSubnet(test_subnet_ip_count.TestSubnet, test_case.ApiServerRDBMSTestCase):
    pass

class TestPermissions(test_perms2.TestPermissions, test_case.ApiServerRDBMSTestCase):
    pass

class TestLogicalRouter(test_logical_router.TestLogicalRouter, test_case.ApiServerRDBMSTestCase):
    pass

class TestIpAlloc(test_ip_alloc.TestIpAlloc, test_case.ApiServerRDBMSTestCase):
    def test_notify_doesnt_persist(self):
        pass

class TestForwardingClassId(test_fc_id.TestForwardingClassId, test_case.ApiServerRDBMSTestCase):
    pass

class TestRequestedIp(test_askip.TestRequestedIp, test_case.ApiServerRDBMSTestCase):
    pass
