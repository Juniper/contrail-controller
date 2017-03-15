import sys
import json

from testtools.matchers import Equals, Contains
from testtools import ExpectedException
import webtest.app
import cfgm_common.exceptions

sys.path.append('../common/tests')
from test_utils import *
import test_common

import test_case

class TestBasic(test_case.NeutronBackendTestCase):
    def read_resource(self, url_pfx, id):
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': id}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(url_pfx), body)
        return json.loads(resp.text)
    # end read_resource

    def list_resource(self, url_pfx,
        proj_uuid=None, req_fields=None, req_filters=None,
        is_admin=False):
        if proj_uuid == None:
            proj_uuid = self._vnc_lib.fq_name_to_id('project',
            fq_name=['default-domain', 'default-project'])

        context = {'operation': 'READALL',
                   'user_id': '',
                   'tenant_id': proj_uuid,
                   'roles': '',
                   'is_admin': is_admin}
        data = {'fields': req_fields, 'filters': req_filters or {}}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json(
            '/neutron/%s' %(url_pfx), body)
        return json.loads(resp.text)
    # end list_resource

    def create_resource(self, res_type, proj_id, name=None,
                        extra_res_fields=None, is_admin=False):
        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': is_admin,
                   'roles': '',
                   'tenant_id': proj_id}
        if name:
            res_name = name
        else:
            res_name = '%s-%s' % (res_type, self.id())
        data = {'resource': {'name': res_name,
                             'tenant_id': proj_id}}
        if extra_res_fields:
            data['resource'].update(extra_res_fields)

        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        return json.loads(resp.text)

    def delete_resource(self, res_type, proj_id, id, is_admin=False):
        context = {'operation': 'DELETE',
                   'user_id': '',
                   'is_admin': is_admin,
                   'roles': '',
                   'tenant_id': proj_id}

        data = {'id': id}

        body = {'context': context, 'data': data}
        self._api_svr_app.post_json('/neutron/%s' %(res_type), body)

    def update_resource(self, res_type, res_id, proj_id, name=None, extra_res_fields=None,
                        is_admin=False, operation=None):
        context = {'operation': operation or 'UPDATE',
                   'user_id': '',
                   'is_admin': is_admin,
                   'roles': '',
                   'tenant_id': proj_id}
        if name:
            res_name = name
        else:
            res_name = '%s-%s' %(res_type, str(uuid.uuid4()))

        data = {'resource': {'name': res_name,
                             'tenant_id': proj_id},
                'id': res_id}
        if extra_res_fields:
            data['resource'].update(extra_res_fields)

        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        return json.loads(resp.text)
    # end _update_resource

    def test_list_with_inconsistent_members(self):
        # 1. create collection
        # 2. list, verify full collection
        # 3. mess with one in vnc_to_neutron, verify collection-1
        # 4. restore, list, verify full collection
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])

        objects = {}
        for (obj_type, obj_class, create_method_name) in \
            [('virtual_network', vnc_api.VirtualNetwork,
              'virtual_network_create'),
             ('network_ipam', vnc_api.NetworkIpam,
              'network_ipam_create'),
             ('network_policy', vnc_api.NetworkPolicy,
              'network_policy_create'),
             ('logical_router', vnc_api.LogicalRouter,
              'logical_router_create'),
             ('security_group', vnc_api.SecurityGroup,
              'security_group_create'),
             ('route_table', vnc_api.RouteTable,
              'route_table_create'),
             ('service_instance', vnc_api.ServiceInstance,
              'service_instance_create')]:
            objects[obj_type] = [obj_class('%s-%s' %(self.id(), i))
                                 for i in range(3)]
            for obj in objects[obj_type]:
                create_method = getattr(self._vnc_lib, create_method_name)
                create_method(obj)

        objects['virtual_machine_interface'] = \
            [vnc_api.VirtualMachineInterface('%s-%s' %(self.id(), i), proj_obj)
             for i in range(3)]
        for obj in objects['virtual_machine_interface']:
            obj.add_virtual_network(vnc_api.VirtualNetwork())
            self._vnc_lib.virtual_machine_interface_create(obj)

        vn_obj = vnc_api.VirtualNetwork(self.id())
        sn0_id = str(uuid.uuid4())
        sn1_id = str(uuid.uuid4())
        sn2_id = str(uuid.uuid4())
        vn_obj.add_network_ipam(vnc_api.NetworkIpam(),
            vnc_api.VnSubnetsType(
                [vnc_api.IpamSubnetType(vnc_api.SubnetType('1.1.1.0', 28),
                                        subnet_uuid=sn0_id),
                 vnc_api.IpamSubnetType(vnc_api.SubnetType('2.2.2.0', 28),
                                        subnet_uuid=sn1_id),
                 vnc_api.IpamSubnetType(vnc_api.SubnetType('3.3.3.0', 28),
                                        subnet_uuid=sn2_id)]))
        self._vnc_lib.virtual_network_create(vn_obj)

        fip_pool_obj = vnc_api.FloatingIpPool(self.id(), vn_obj)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        objects['floating_ip'] = [vnc_api.FloatingIp('%s-%s' %(self.id(), i),
            fip_pool_obj)
            for i in range(3)]
        for obj in objects['floating_ip']:
            obj.add_project(proj_obj)
            self._vnc_lib.floating_ip_create(obj)

        collection_types = [
            (objects['virtual_network'], 'network',
             '_network_vnc_to_neutron'),
            (objects['virtual_machine_interface'], 'port',
             '_port_vnc_to_neutron'),
            (objects['network_ipam'], 'ipam',
             '_ipam_vnc_to_neutron'),
            (objects['network_policy'], 'policy',
             '_policy_vnc_to_neutron'),
            (objects['logical_router'], 'router',
             '_router_vnc_to_neutron'),
            (objects['floating_ip'], 'floatingip',
             '_floatingip_vnc_to_neutron'),
            (objects['security_group'], 'security_group',
             '_security_group_vnc_to_neutron'),
            (objects['route_table'], 'route_table',
             '_route_table_vnc_to_neutron'),
            (objects['service_instance'], 'nat_instance',
             '_svc_instance_vnc_to_neutron'),
        ]

        def list_resource(url_pfx):
            context = {'operation': 'READALL',
                       'user_id': '',
                       'tenant_id': proj_obj.uuid,
                       'roles': '',
                       'is_admin': 'False'}
            data = {'fields': None, 'filters': {}}
            body = {'context': context, 'data': data}
            resp = self._api_svr_app.post_json(
                '/neutron/%s' %(url_pfx), body)
            return json.loads(resp.text)

        # for collections that are objects in contrail model
        for (objects, res_url_pfx, res_xlate_name) in collection_types:
            res_dicts = list_resource(res_url_pfx)
            present_ids = [r['id'] for r in res_dicts] 
            for obj in objects:
                self.assertIn(obj.uuid, present_ids)

            neutron_api_obj = FakeExtensionManager.get_extension_objects(
                'vnc_cfg_api.neutronApi')[0]
            neutron_db_obj = neutron_api_obj._npi._cfgdb

            def err_on_object_2(orig_method, res_obj, *args, **kwargs):
                if res_obj.uuid == objects[2].uuid:
                    raise Exception('faking inconsistent element')
                return orig_method(res_obj, *args, **kwargs)

            with test_common.patch(
                neutron_db_obj, res_xlate_name, err_on_object_2):
                res_dicts = list_resource(res_url_pfx)
                present_ids = [r['id'] for r in res_dicts] 
                self.assertNotIn(objects[2].uuid, present_ids)

            res_dicts = list_resource(res_url_pfx)
            present_ids = [r['id'] for r in res_dicts] 
            for obj in objects:
                self.assertIn(obj.uuid, present_ids)
        # end for collections that are objects in contrail model

        # subnets, sg-rules etc.
        res_dicts = list_resource('subnet')
        present_ids = [r['id'] for r in res_dicts]
        for sn_id in [sn0_id, sn1_id, sn2_id]:
            self.assertIn(sn_id, present_ids)

        def err_on_sn2(orig_method, subnet_vnc, *args, **kwargs):
            if subnet_vnc.subnet_uuid == sn2_id:
                raise Exception('faking inconsistent element')
            return orig_method(subnet_vnc, *args, **kwargs)

        with test_common.patch(
            neutron_db_obj, '_subnet_vnc_to_neutron', err_on_sn2):
            res_dicts = list_resource('subnet')
            present_ids = [r['id'] for r in res_dicts] 
            self.assertNotIn(sn2_id, present_ids)
    # end test_list_with_inconsistent_members

    def test_subnet_uuid_heal(self):
        # 1. create 2 subnets thru vnc_api
        # 2. mess with useragent-kv index for one
        # 3. neutron subnet list should report fine
        # 4. neutron subnet list with uuids where
        #    one is a fake uuid shouldnt cause error in list
        ipam_obj = vnc_api.NetworkIpam('ipam-%s' %(self.id()))
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj = vnc_api.VirtualNetwork('vn1-%s' %(self.id()))
        sn1_uuid = str(uuid.uuid4())
        vn1_obj.add_network_ipam(ipam_obj,
            vnc_api.VnSubnetsType(
                [vnc_api.IpamSubnetType(vnc_api.SubnetType('1.1.1.0', 28),
                                        subnet_uuid=sn1_uuid)]))
        self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_obj = vnc_api.VirtualNetwork('vn2-%s' %(self.id()))
        sn2_uuid = str(uuid.uuid4())
        vn2_obj.add_network_ipam(ipam_obj,
            vnc_api.VnSubnetsType(
                [vnc_api.IpamSubnetType(vnc_api.SubnetType('2.2.2.0', 28),
                                        subnet_uuid=sn2_uuid)]))
        self._vnc_lib.virtual_network_create(vn2_obj)

        # the list primes cfgdb handle(conn to api server)
        self.list_resource('subnet')
        neutron_api_obj = FakeExtensionManager.get_extension_objects(
            'vnc_cfg_api.neutronApi')[0]
        neutron_db_obj = neutron_api_obj._npi._cfgdb

        heal_invoked = [False]
        def verify_heal_invoked(orig_method, *args, **kwargs):
            heal_invoked[0] = True
            return orig_method(*args, **kwargs)
        with test_common.patch(
            neutron_db_obj, 'subnet_id_heal', verify_heal_invoked):
            with CassandraCFs.get_cf(
                'useragent_keyval_table').patch_row(sn2_uuid, None):
                # verify heal
                rsp = self.list_resource('subnet',
                    req_filters={'id': [sn1_uuid, sn2_uuid]})
                self.assertEqual(heal_invoked[0], True)
                self.assertEqual(len(rsp), 2)
                self.assertEqual(set([r['id'] for r in rsp]),
                                 set([sn1_uuid, sn2_uuid]))

                # verify wrong/non-existent id doesn't cause
                # list to have error
                heal_invoked[0] = False
                fake_uuid = str(uuid.uuid4())
                rsp = self.list_resource('subnet',
                    req_filters={'id': [sn1_uuid, sn2_uuid, fake_uuid]})
                self.assertEqual(heal_invoked[0], True)
                self.assertEqual(len(rsp), 2)
                self.assertEqual(set([r['id'] for r in rsp]),
                                 set([sn1_uuid, sn2_uuid]))

                # verify read of non-existent id throws exception
                # and valid one doesn't
                with ExpectedException(webtest.app.AppError):
                    self.read_resource('subnet', fake_uuid)
                self.read_resource('subnet', sn1_uuid)
    # end test_subnet_uuid_heal

    def test_extra_fields_on_network(self):
        test_obj = self._create_test_object()
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': test_obj.uuid}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/network', body)
        net_dict = json.loads(resp.text)
        self.assertIn('contrail:fq_name', net_dict)
    # end test_extra_fields_on_network

    def test_port_bindings(self):
        vn_obj = vnc_api.VirtualNetwork(self.id())
        vn_obj.add_network_ipam(vnc_api.NetworkIpam(),
            vnc_api.VnSubnetsType(
                [vnc_api.IpamSubnetType(
                         vnc_api.SubnetType('1.1.1.0', 24))]))
        self._vnc_lib.virtual_network_create(vn_obj)

        sg_obj = vnc_api.SecurityGroup('default')
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except cfgm_common.exceptions.RefsExistError:
            # it might already have been created/fetched
            pass

        proj_uuid = self._vnc_lib.fq_name_to_id('project',
            fq_name=['default-domain', 'default-project'])

        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': True,
                   'roles': ''}
        data = {'resource':{'network_id': vn_obj.uuid,
                            'tenant_id': proj_uuid,
                            'binding:profile': {'foo': 'bar'},
                            'binding:host_id': 'somehost'}}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/port', body)
        port_dict = json.loads(resp.text)
        self.assertTrue(isinstance(port_dict['binding:profile'], dict))
        self.assertTrue(isinstance(port_dict['binding:host_id'], basestring))
    # end test_port_bindings

    def test_floating_ip_list(self):
        proj_objs = []
        for i in range(3):
            proj_id = str(uuid.uuid4())
            proj_name = 'proj-%s-%s' %(self.id(), i)
            test_case.get_keystone_client().tenants.add_tenant(proj_id, proj_name)
            proj_objs.append(self._vnc_lib.project_read(id=proj_id))

        sg_q_list = [self.create_resource('security_group', proj_objs[i].uuid)
                     for i in range(3)]

        # public network on last project
        pub_net1_q = self.create_resource('network', proj_objs[-1].uuid,
            name='public-network-%s-1' %(self.id()),
            extra_res_fields={'router:external': True})
        self.create_resource('subnet', proj_objs[-1].uuid,
            name='public-subnet-%s-1' %(self.id()),
            extra_res_fields={
                'network_id': pub_net1_q['id'],
                'cidr': '10.1.1.0/24',
                'ip_version': 4,
            })
        pub_net2_q = self.create_resource('network', proj_objs[-1].uuid,
            name='public-network-%s-2' %(self.id()),
            extra_res_fields={'router:external': True})
        self.create_resource('subnet', proj_objs[-1].uuid,
            name='public-subnet-%s-2' %(self.id()),
            extra_res_fields={
                'network_id': pub_net2_q['id'],
                'cidr': '20.1.1.0/24',
                'ip_version': 4,
            })

        def create_net_subnet_port_assoc_fip(i, pub_net_q_list,
                                             has_routers=True):
            net_q_list = [self.create_resource('network', proj_objs[i].uuid,
                name='network-%s-%s-%s' %(self.id(), i, j)) for j in range(2)]
            subnet_q_list = [self.create_resource('subnet', proj_objs[i].uuid,
                name='subnet-%s-%s-%s' %(self.id(), i, j),
                extra_res_fields={
                    'network_id': net_q_list[j]['id'],
                    'cidr': '1.%s.%s.0/24' %(i, j),
                    'ip_version': 4,
                }) for j in range(2)]

            if has_routers:
                router_q_list = [self.create_resource('router', proj_objs[i].uuid,
                    name='router-%s-%s-%s' %(self.id(), i, j),
                    extra_res_fields={
                        'external_gateway_info': {
                            'network_id': pub_net_q_list[j]['id'],
                        }
                    }) for j in range(2)]
                [self.update_resource('router', router_q_list[j]['id'],
                    proj_objs[i].uuid, is_admin=True, operation='ADDINTERFACE',
                    extra_res_fields={'subnet_id': subnet_q_list[j]['id']})
                        for j in range(2)]
            else:
                router_q_list = None

            port_q_list = [self.create_resource('port', proj_objs[i].uuid,
                name='port-%s-%s-%s' %(self.id(), i, j),
                extra_res_fields={
                    'network_id': net_q_list[j]['id'],
                    'security_groups': [sg_q_list[i]['id']],
                }) for j in range(2)]

            fip_q_list = [self.create_resource('floatingip', proj_objs[i].uuid,
                name='fip-%s-%s-%s' %(self.id(), i, j),
                is_admin=True,
                extra_res_fields={'floating_network_id': pub_net_q_list[j]['id'],
                                  'port_id': port_q_list[j]['id']}) for j in range(2)]

            return {'network': net_q_list, 'subnet': subnet_q_list,
                    'ports': port_q_list, 'fips': fip_q_list,
                    'routers': router_q_list}
        # end create_net_subnet_port_assoc_fip

        created = []
        # without routers
        created.append(create_net_subnet_port_assoc_fip(
            0, [pub_net1_q, pub_net2_q], has_routers=False))

        # with routers
        created.append(create_net_subnet_port_assoc_fip(
            1, [pub_net1_q, pub_net2_q], has_routers=True))

        # 1. list as admin for all routers
        fip_dicts = self.list_resource('floatingip', is_admin=True)
        # convert list to dict by id
        fip_dicts = dict((fip['id'], fip) for fip in fip_dicts)
        # assert all floatingip we created recevied back
        for fip in created[0]['fips'] + created[1]['fips']:
            self.assertIn(fip['id'], fip_dicts.keys())

        # assert router-id present in fips of proj[1]
        self.assertEqual(created[1]['routers'][0]['id'],
            fip_dicts[created[1]['fips'][0]['id']]['router_id'])
        self.assertEqual(created[1]['routers'][1]['id'],
            fip_dicts[created[1]['fips'][1]['id']]['router_id'])

        # assert router-id not present in fips of proj[0]
        self.assertEqual(None,
            fip_dicts[created[0]['fips'][0]['id']]['router_id'])
        self.assertEqual(None,
            fip_dicts[created[0]['fips'][1]['id']]['router_id'])

        # 2. list routers within project
        fip_dicts = self.list_resource(
            'floatingip', proj_uuid=proj_objs[0].uuid)
        self.assertEqual(None,
            fip_dicts[0]['router_id'])
        self.assertEqual(None,
            fip_dicts[1]['router_id'])
        # convert list to dict by port-id
        fip_dicts = dict((fip['port_id'], fip) for fip in fip_dicts)
        # assert fips point to right port
        self.assertEqual(created[0]['ports'][0]['fixed_ips'][0]['ip_address'],
            fip_dicts[created[0]['ports'][0]['id']]['fixed_ip_address'])
        self.assertEqual(created[0]['ports'][1]['fixed_ips'][0]['ip_address'],
            fip_dicts[created[0]['ports'][1]['id']]['fixed_ip_address'])
    # end test_floating_ip_list

# end class TestBasic


class TestExtraFieldsPresenceByKnob(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestExtraFieldsPresenceByKnob, cls).setUpClass(
            extra_config_knobs=[('NEUTRON', 'contrail_extensions_enabled', True)])

    def test_extra_fields_on_network(self):
        test_obj = self._create_test_object()
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': test_obj.uuid}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/network', body)
        net_dict = json.loads(resp.text)
        self.assertIn('contrail:fq_name', net_dict)
    # end test_extra_fields_on_network
# end class TestExtraFieldsPresenceByKnob

class TestExtraFieldsAbsenceByKnob(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestExtraFieldsAbsenceByKnob, cls).setUpClass(
            extra_config_knobs=[('NEUTRON', 'contrail_extensions_enabled', False)])

    def test_no_extra_fields_on_network(self):
        test_obj = self._create_test_object()
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': test_obj.uuid}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/network', body)
        net_dict = json.loads(resp.text)
        self.assertNotIn('contrail:fq_name', net_dict)
    # end test_extra_fields_on_network
# end class TestExtraFieldsAbsenceByKnob
