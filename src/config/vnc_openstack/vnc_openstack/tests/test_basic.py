import sys
import json
import re
import mock

from testtools import ExpectedException
import webtest.app
import datetime

sys.path.append('../common/tests')
from cfgm_common.exceptions import NoIdError
from cfgm_common import PERMS_RWX, PERMS_NONE, PERMS_RX
from test_utils import *
import test_common
import requests
import test_case

try:
    from neutron_lib import constants
except ImportError:
    from neutron.common import constants

_IFACE_ROUTE_TABLE_NAME_PREFIX = 'NEUTRON_IFACE_RT'


class TestBasic(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestBasic, cls).setUpClass(
            extra_config_knobs=[
                ('DEFAULTS', 'apply_subnet_host_routes', True)
            ])

    def read_resource(self, url_pfx, id):
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': '',
                   'is_admin': True}
        data = {'fields': None,
                'id': id}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' % url_pfx, body)
        return json.loads(resp.text)
    # end read_resource

    def list_resource(self, url_pfx, proj_uuid=None, req_fields=None,
                      req_filters=None, is_admin=False):
        if proj_uuid is None:
            proj_uuid = self._vnc_lib.fq_name_to_id(
                'project', fq_name=['default-domain', 'default-project'])

        context = {'operation': 'READALL',
                   'user_id': '',
                   'tenant_id': proj_uuid,
                   'roles': '',
                   'is_admin': is_admin}
        data = {'fields': req_fields, 'filters': req_filters or {}}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json(
            '/neutron/%s' % url_pfx, body)
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
        resp = self._api_svr_app.post_json('/neutron/%s' % res_type, body)
        return json.loads(resp.text)

    def delete_resource(self, res_type, proj_id, id, is_admin=False):
        context = {'operation': 'DELETE',
                   'user_id': '',
                   'is_admin': is_admin,
                   'roles': '',
                   'tenant_id': proj_id}

        data = {'id': id}

        body = {'context': context, 'data': data}
        self._api_svr_app.post_json('/neutron/%s' % res_type, body)

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
        for (objs, res_url_pfx, res_xlate_name) in collection_types:
            res_dicts = list_resource(res_url_pfx)
            present_ids = [r['id'] for r in res_dicts]
            for obj in objs:
                self.assertIn(obj.uuid, present_ids)

            neutron_api_obj = FakeExtensionManager.get_extension_objects(
                'vnc_cfg_api.neutronApi')[0]
            neutron_db_obj = neutron_api_obj._npi._cfgdb

            def err_on_object_2(orig_method, res_obj, *args, **kwargs):
                if res_obj.uuid == objs[2].uuid:
                    raise Exception('faking inconsistent element')
                return orig_method(res_obj, *args, **kwargs)

            with test_common.patch(
                neutron_db_obj, res_xlate_name, err_on_object_2):
                res_dicts = list_resource(res_url_pfx)
                present_ids = [r['id'] for r in res_dicts]
                self.assertNotIn(objs[2].uuid, present_ids)

            res_dicts = list_resource(res_url_pfx)
            present_ids = [r['id'] for r in res_dicts]
            for obj in objs:
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
        for obj_type, obj_list in objects.items():
            delete_method = getattr(self._vnc_lib, obj_type+'_delete')
            for obj in obj_list:
                delete_method(id=obj.uuid)
    # end test_list_with_inconsistent_members

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

        vr_obj = vnc_api.VirtualRouter("somehost")
        vr_obj = self._vnc_lib.virtual_router_create(vr_obj)

        sg_obj = vnc_api.SecurityGroup('default')
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except vnc_api.RefsExistError:
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

    def test_sub_interfaces_with_vm_attached(self):
        vn = vnc_api.VirtualNetwork('vn-%s' % (self.id()))
        self._vnc_lib.virtual_network_create(vn)
        vmi_prop = vnc_api.VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=256)
        vmi_name = 'vmi1'
        vm_name = 'vm1'
        vm = vnc_api.VirtualMachine(vm_name)
        self._vnc_lib.virtual_machine_create(vm)
        vmi_obj = vnc_api.VirtualMachineInterface(vmi_name, parent_obj=vnc_api.Project())
        vmi_obj.set_virtual_network(vn)
        vmi_obj.add_virtual_machine(vm)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        vmi_name = 'sub_vmi1'
        sub_vmi_obj = vnc_api.VirtualMachineInterface(vmi_name, parent_obj=vnc_api.Project(),
                                                      virtual_machine_interface_properties=vmi_prop)
        sub_vmi_obj.set_virtual_network(vn)
        sub_vmi_obj.set_virtual_machine_interface(vmi_obj)
        self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj)
        sub_intf_dict = self.read_resource('port', sub_vmi_obj.uuid)
        self.assertEqual(sub_intf_dict['status'], constants.PORT_STATUS_ACTIVE)

        # Clean the resources
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=sub_vmi_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm.uuid)

    # end test_sub_interfaces_with_vm_attached

    def test_sub_interfaces_with_no_vm_attached(self):
        vn = vnc_api.VirtualNetwork('vn-%s' % (self.id()))
        self._vnc_lib.virtual_network_create(vn)
        vmi_prop = vnc_api.VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=256)
        vmi_name = 'vmi2'
        vmi_obj = vnc_api.VirtualMachineInterface(
            vmi_name, parent_obj=vnc_api.Project())
        vmi_obj.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        vmi_name = 'sub_vmi2'
        sub_vmi_obj = vnc_api.VirtualMachineInterface(
            vmi_name, parent_obj=vnc_api.Project(),
            virtual_machine_interface_properties=vmi_prop)
        sub_vmi_obj.set_virtual_network(vn)
        sub_vmi_obj.set_virtual_machine_interface(vmi_obj)
        self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj)
        sub_intf_dict = self.read_resource('port', sub_vmi_obj.uuid)
        self.assertEqual(sub_intf_dict['status'], constants.PORT_STATUS_DOWN)

        # Clean the resources
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=sub_vmi_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

    # end test_sub_interfaces_with_no_vm_attached

    def test_sg_rules_delete_when_peer_group_deleted_on_read_sg(self):
        sg1_obj = vnc_api.SecurityGroup('sg1-%s' %(self.id()))
        self._vnc_lib.security_group_create(sg1_obj)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.fq_name)
        sg2_obj = vnc_api.SecurityGroup('sg2-%s' %(self.id()))
        self._vnc_lib.security_group_create(sg2_obj)
        sg2_obj = self._vnc_lib.security_group_read(sg2_obj.fq_name)
        sgr_uuid = str(uuid.uuid4())
        local = [vnc_api.AddressType(security_group='local')]
        remote = [vnc_api.AddressType(security_group=sg2_obj.get_fq_name_str())]
        sgr_obj = vnc_api.PolicyRuleType(rule_uuid=sgr_uuid,
                                         direction='>',
                                         protocol='any',
                                         src_addresses=remote,
                                         src_ports=[vnc_api.PortType(0, 255)],
                                         dst_addresses=local,
                                         dst_ports=[vnc_api.PortType(0, 255)],
                                         ethertype='IPv4')
        rules = vnc_api.PolicyEntriesType([sgr_obj])
        sg1_obj.set_security_group_entries(rules)
        self._vnc_lib.security_group_update(sg1_obj)

        self._vnc_lib.security_group_delete(fq_name=sg2_obj.fq_name)

        sg_dict = self.read_resource('security_group', sg1_obj.uuid)
        sgr = [rule['id'] for rule in sg_dict.get('security_group_rules', [])]
        self.assertNotIn(sgr_uuid, sgr)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.fq_name)
        sgr = [rule.rule_uuid for rule in
               sg1_obj.get_security_group_entries().get_policy_rule() or []]
        self.assertIn(sgr_uuid, sgr)

    def test_sg_rules_delete_when_peer_group_deleted_on_read_rule(self):
        sg1_obj = vnc_api.SecurityGroup('sg1-%s' %(self.id()))
        self._vnc_lib.security_group_create(sg1_obj)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.fq_name)
        sg2_obj = vnc_api.SecurityGroup('sg2-%s' %(self.id()))
        self._vnc_lib.security_group_create(sg2_obj)
        sg2_obj = self._vnc_lib.security_group_read(sg2_obj.fq_name)
        sgr_uuid = str(uuid.uuid4())
        local = [vnc_api.AddressType(security_group='local')]
        remote = [vnc_api.AddressType(
            security_group=sg2_obj.get_fq_name_str())]
        sgr_obj = vnc_api.PolicyRuleType(rule_uuid=sgr_uuid,
                                         direction='>',
                                         protocol='any',
                                         src_addresses=remote,
                                         src_ports=[vnc_api.PortType(0, 255)],
                                         dst_addresses=local,
                                         dst_ports=[vnc_api.PortType(0, 255)],
                                         ethertype='IPv4')
        rules = vnc_api.PolicyEntriesType([sgr_obj])
        sg1_obj.set_security_group_entries(rules)
        self._vnc_lib.security_group_update(sg1_obj)

        self._vnc_lib.security_group_delete(fq_name=sg2_obj.fq_name)

        with ExpectedException(webtest.app.AppError):
            self.read_resource('security_group_rule', sgr_uuid)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.fq_name)
        sgr = [rule.rule_uuid for rule in
               sg1_obj.get_security_group_entries().get_policy_rule() or []]
        self.assertIn(sgr_uuid, sgr)

    def test_sg_rules_delete_when_peer_group_deleted_on_list_rules(self):
        sg1_obj = vnc_api.SecurityGroup('sg1-%s' %(self.id()))
        self._vnc_lib.security_group_create(sg1_obj)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.fq_name)
        sg2_obj = vnc_api.SecurityGroup('sg2-%s' %(self.id()))
        self._vnc_lib.security_group_create(sg2_obj)
        sg2_obj = self._vnc_lib.security_group_read(sg2_obj.fq_name)
        sgr_uuid = str(uuid.uuid4())
        local = [vnc_api.AddressType(security_group='local')]
        remote = [vnc_api.AddressType(
            security_group=sg2_obj.get_fq_name_str())]
        sgr_obj = vnc_api.PolicyRuleType(rule_uuid=sgr_uuid,
                                         direction='>',
                                         protocol='any',
                                         src_addresses=remote,
                                         src_ports=[vnc_api.PortType(0, 255)],
                                         dst_addresses=local,
                                         dst_ports=[vnc_api.PortType(0, 255)],
                                         ethertype='IPv4')
        rules = vnc_api.PolicyEntriesType([sgr_obj])
        sg1_obj.set_security_group_entries(rules)
        self._vnc_lib.security_group_update(sg1_obj)

        self._vnc_lib.security_group_delete(fq_name=sg2_obj.fq_name)

        sgr_dict = self.list_resource('security_group_rule')
        self.assertNotIn(sgr_uuid, [rule['id'] for rule in sgr_dict])
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.fq_name)
        sgr = [rule.rule_uuid for rule in
               sg1_obj.get_security_group_entries().get_policy_rule() or []]
        self.assertIn(sgr_uuid, sgr)

    def test_sg_list_with_remote(self):
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        sg1_dict = self.create_resource('security_group',
                                       proj_obj.uuid,
                                       'sg1-%s' % self.id())
        sg2_dict = self.create_resource('security_group',
                                       proj_obj.uuid,
                                       'sg2-%s' % self.id())
        sgr1_dict = self.create_resource('security_group_rule',
                                       proj_obj.uuid,
                                       'sgr1-%s' % self.id(),
                                       extra_res_fields={
                                           'security_group_id': sg1_dict['id'],
                                           'remote_ip_prefix': None,
                                           'remote_group_id': sg2_dict['id'],
                                           'port_range_min': None,
                                           'port_range_max': None,
                                           'protocol': None,
                                           'ethertype': None,
                                           'direction': 'egress',
                                       }
                                       )
        sgr2_dict = self.create_resource('security_group_rule',
                                       proj_obj.uuid,
                                       'sgr2-%s' % self.id(),
                                       extra_res_fields={
                                           'security_group_id': sg2_dict['id'],
                                           'remote_ip_prefix': None,
                                           'remote_group_id': sg1_dict['id'],
                                           'port_range_min': None,
                                           'port_range_max': None,
                                           'protocol': None,
                                           'ethertype': None,
                                           'direction': 'ingress',
                                       }
                                       )
        sg_list = self.list_resource('security_group', proj_obj.uuid)
        found = 0
        for sg in sg_list:
            if sg['id'] == sg1_dict['id']:
                for rule in sg['security_group_rules']:
                    if rule['direction'] == 'ingress':
                        self.assertEqual(rule['remote_group_id'], sg2_dict['id'])
                found += 1
            if sg['id'] == sg2_dict['id']:
                for rule in sg['security_group_rules']:
                    if rule['direction'] == 'ingress':
                        self.assertEqual(rule['remote_group_id'], sg1_dict['id'])
                found += 1
        self.assertEqual(found, 2)

    def test_delete_irt_for_subnet_host_route(self):
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        ipam_obj = vnc_api.NetworkIpam('ipam-%s' % self.id())
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn_obj = vnc_api.VirtualNetwork('vn-%s' % self.id())
        sn_uuid = str(uuid.uuid4())
        vn_obj.add_network_ipam(
            ipam_obj,
            vnc_api.VnSubnetsType([
                vnc_api.IpamSubnetType(
                    vnc_api.SubnetType('1.1.1.0', 28),
                    subnet_uuid=sn_uuid,
                    host_routes=vnc_api.RouteTableType([
                        vnc_api.RouteType(
                            prefix='2.2.2.0/28',
                            next_hop='1.1.1.3'
                        )
                    ])
                )
            ])
        )
        self._vnc_lib.virtual_network_create(vn_obj)
        # Create default sg as vnc_openstack hooks are disabled in that ut
        sg_obj = vnc_api.SecurityGroup('default')
        self._vnc_lib.security_group_create(sg_obj)
        port_dict = self.create_resource('port',
                                         proj_obj.uuid,
                                         'vmi-%s' % self.id(),
                                         extra_res_fields={
                                             'network_id': vn_obj.uuid,
                                             'fixed_ips': [{
                                                 'ip_address': '1.1.1.3'
                                             }]
                                         })
        route_table = vnc_api.RouteTableType('irt-%s' % self.id())
        route_table.set_route([])
        irt_obj = vnc_api.InterfaceRouteTable(
            interface_route_table_routes=route_table,
            name='irt-%s' % self.id())
        self._vnc_lib.interface_route_table_create(irt_obj)
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=port_dict['id'])
        vmi_obj.add_interface_route_table(irt_obj)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        self.delete_resource('port', vmi_obj.parent_uuid, vmi_obj.uuid)

        host_route_irt_fq_name = proj_obj.fq_name + ['%s_%s_%s' % (
            _IFACE_ROUTE_TABLE_NAME_PREFIX, sn_uuid, vmi_obj.uuid)]
        with ExpectedException(NoIdError):
            self._vnc_lib.interface_route_table_read(host_route_irt_fq_name)
        try:
            irt_obj = self._vnc_lib.interface_route_table_read(id=irt_obj.uuid)
        except NoIdError:
            self.fail("The manually added interface route table as been "
                      "automatically removed")
        self.assertIsNone(irt_obj.get_virtual_machine_interface_back_refs())

    def _create_port_with_sg(self, proj_id, port_security):
        net_q = self.create_resource('network', proj_id)
        subnet_q = self.create_resource('subnet', proj_id, extra_res_fields={'network_id': net_q['id'], 'cidr': '10.2.0.0/24', 'ip_version': 4})
        sg_q = self.create_resource('security_group', proj_id)
        return self.create_resource('port', proj_id, extra_res_fields={'network_id': net_q['id'], 'security_groups': [sg_q['id']], 'port_security_enabled':port_security})

    def _create_port_with_no_sg(self, proj_id):
        net_q = self.create_resource('network', proj_id)
        subnet_q = self.create_resource('subnet', proj_id, extra_res_fields={'network_id': net_q['id'], 'cidr': '10.2.0.0/24', 'ip_version': 4})
        return self.create_resource('port', proj_id, extra_res_fields={'network_id': net_q['id'], 'port_security_enabled':False})

    def test_create_port_with_port_security_disabled_and_sg(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        with ExpectedException(webtest.app.AppError):
            self._create_port_with_sg(proj_obj.uuid, False)

    def test_empty_list_for_allowed_address(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        port_q = self._create_port_with_no_sg(proj_obj.uuid)
	self.assertTrue(port_q['allowed_address_pairs'] is not None)

    def test_update_port_with_port_security_disabled_and_sg(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        port_q = self._create_port_with_sg(proj_obj.uuid, True)
        with ExpectedException(webtest.app.AppError):
            self.update_resource('port', port_q['id'], proj_obj.uuid, extra_res_fields={'port_security_enabled':False})

    def test_update_port_with_security_group_and_port_security_disabled(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        port_q = self._create_port_with_no_sg(proj_obj.uuid)
        sg_q = self.create_resource('security_group', proj_obj.uuid)
        with ExpectedException(webtest.app.AppError):
            self.update_resource('port', port_q['id'], proj_obj.uuid, extra_res_fields={'security_groups': [sg_q['id']]})

    def test_fixed_ip_conflicts_with_floating_ip(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        sg_q = self.create_resource('security_group', proj_obj.uuid)
        net_q = self.create_resource('network', proj_obj.uuid,
            extra_res_fields={'router:external': True,
                              'port_security_enabled': True})
        subnet_q = self.create_resource('subnet', proj_obj.uuid,
            extra_res_fields={
                'network_id': net_q['id'],
                'cidr': '1.1.1.0/24',
                'ip_version': 4,
            })
        fip_q = self.create_resource('floatingip', proj_obj.uuid,
            extra_res_fields={'floating_network_id': net_q['id']})

        try:
            self.create_resource('port', proj_obj.uuid,
                extra_res_fields={
                    'network_id': net_q['id'],
                    'fixed_ips': [{'ip_address': fip_q['floating_ip_address']}],
                    'security_groups': [sg_q['id']],
                })
            self.assertTrue(False,
                'Create with fixed-ip conflicting with floating-ip passed')
        except webtest.app.AppError as e:
            self.assertIsNot(re.search('Conflict', str(e)), None)
            self.assertIsNot(re.search('Ip address already in use', str(e)),
                             None)

        # cleanup
        self.delete_resource('floatingip', proj_obj.uuid, fip_q['id'])
        self.delete_resource('subnet', proj_obj.uuid, subnet_q['id'])
        self.delete_resource('network', proj_obj.uuid, net_q['id'])
        self.delete_resource('security_group', proj_obj.uuid, sg_q['id'])
    # end test_fixed_ip_conflicts_with_floating_ip

    def test_empty_floating_ip_body_disassociates(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        sg_q = self.create_resource('security_group', proj_obj.uuid)

        pvt_net_q = self.create_resource('network', proj_obj.uuid,
            extra_res_fields={'router:external': True,
                              'port_security_enabled': True})
        pvt_subnet_q = self.create_resource('subnet', proj_obj.uuid,
            extra_res_fields={
                'network_id': pvt_net_q['id'],
                'cidr': '1.1.1.0/24',
                'ip_version': 4,
            })
        port_q = self.create_resource('port', proj_obj.uuid,
            extra_res_fields={
                'network_id': pvt_net_q['id'],
                'security_groups': [sg_q['id']],
            })


        pub_net_q = self.create_resource('network', proj_obj.uuid,
            extra_res_fields={'router:external': True})
        pub_subnet_q = self.create_resource('subnet', proj_obj.uuid,
            extra_res_fields={
                'network_id': pub_net_q['id'],
                'cidr': '10.1.1.0/24',
                'ip_version': 4,
            })
        fip_q = self.create_resource('floatingip', proj_obj.uuid,
            extra_res_fields={
                'floating_network_id': pub_net_q['id'],
                'port_id': port_q['id'],
            })

        # update fip with no 'resource' key and assert port disassociated
        context = {'operation': 'UPDATE',
                   'user_id': '',
                   'is_admin': False,
                   'roles': '',
                   'tenant_id': proj_obj.uuid}
        data = {'id': fip_q['id']}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/floatingip', body)
        self.assertEqual(resp.status_code, 200)
        fip_read = self.read_resource('floatingip', fip_q['id'])
        self.assertEqual(fip_read['port_id'], None)

        # cleanup
        self.delete_resource('port', proj_obj.uuid, port_q['id'])
        self.delete_resource('subnet', proj_obj.uuid, pvt_subnet_q['id'])
        self.delete_resource('network', proj_obj.uuid, pvt_net_q['id'])
        self.delete_resource('floatingip', proj_obj.uuid, fip_q['id'])
        self.delete_resource('subnet', proj_obj.uuid, pub_subnet_q['id'])
        self.delete_resource('network', proj_obj.uuid, pub_net_q['id'])
        self.delete_resource('security_group', proj_obj.uuid, sg_q['id'])
    # end test_empty_floating_ip_body_disassociates

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
                name='network-%s-%s-%s' %(self.id(), i, j),
                extra_res_fields={'port_security_enabled': True}) for j in range(2)]
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

    def test_subnet_delete_when_port_is_present(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        sg_q = self.create_resource('security_group', proj_obj.uuid)
        net_q = self.create_resource('network', proj_obj.uuid)
        subnet_q = self.create_resource('subnet', proj_obj.uuid,
            extra_res_fields={
                'network_id': net_q['id'],
                'cidr': '1.1.1.0/24',
                'ip_version': 4,
            })
        port_q = self.create_resource('port', proj_obj.uuid,
            extra_res_fields={
                'network_id': net_q['id'],
                'security_groups': [sg_q['id']],
            })

        with ExpectedException(webtest.app.AppError):
            self.delete_resource('subnet', proj_obj.uuid, subnet_q['id'])

        # cleanup
        self.delete_resource('port', proj_obj.uuid, port_q['id'])
        self.delete_resource('subnet', proj_obj.uuid, subnet_q['id'])
        self.delete_resource('network', proj_obj.uuid, net_q['id'])
        self.delete_resource('security_group', proj_obj.uuid, sg_q['id'])
    # end test_subnet_delete_when_port_is_present

    def test_create_fip_w_port_associated_w_another_fip_negative(self):
        proj_obj = vnc_api.Project('proj-%s' %(self.id()), vnc_api.Domain())
        self._vnc_lib.project_create(proj_obj)
        proj_id = proj_obj.uuid
        # external network
        net_q = self.create_resource('network', proj_id,
                                      extra_res_fields={'router:external':True})
        subnet_q = self.create_resource('subnet', proj_id,
                                         extra_res_fields=
                                         {'network_id': net_q['id'],
                                          'cidr': '10.2.0.0/24',
                                          'ip_version': 4})

        # private network
        pvt_net_q = self.create_resource(
            'network', proj_id,
            extra_res_fields={'port_security_enabled': True},
        )
        pvt_subnet_q = self.create_resource('subnet', proj_id,
                                             extra_res_fields=
                                             {'network_id': pvt_net_q['id'],
                                              'cidr': '20.1.0.0/24',
                                              'ip_version': 4})

        sg_q = self.create_resource('security_group', proj_id)
        port_q = self.create_resource('port', proj_id,
                                       extra_res_fields=
                                       {'network_id':
                                        pvt_subnet_q['network_id'],
                                        'security_groups': [sg_q['id']]})

        fip_q = self.create_resource('floatingip', proj_id,
                                      extra_res_fields=
                                      {'floating_network_id': net_q['id'],
                                       'port_id': port_q['id']})

        # updating a fip should be succesull that already has an assoc. port
        self.update_resource('floatingip', fip_q['id'], proj_id,
                             extra_res_fields={'display_name':'test-fip',
                                               'port_id': port_q['id']})

        # creating a fip with a port that already has another fip associated
        # should fail.
        with ExpectedException(webtest.app.AppError):
            fip_q_2 = self.create_resource('floatingip', proj_id,
                extra_res_fields={'floating_network_id': net_q['id'],
                                  'port_id': port_q['id']})

        # cleanup
        self.delete_resource('floatingip', proj_id, fip_q['id'])
        self.delete_resource('port', proj_id, port_q['id'])
        self.delete_resource('subnet', proj_id, subnet_q['id'])
        self.delete_resource('subnet', proj_id, pvt_subnet_q['id'])
        self.delete_resource('security_group', proj_id, sg_q['id'])
        self.delete_resource('network', proj_id, net_q['id'])
        self.delete_resource('network', proj_id, pvt_net_q['id'])
    # end test_create_fip_w_port_associated_w_another_fip_negative

    def test_network_timestamps(self):
        vn_obj = vnc_api.VirtualNetwork(self.id())
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_dict = self.read_resource('network', vn_obj.uuid)
        # verify created timestamp and updated timestamp are same
        self.assertEqual(vn_dict['created_at'], vn_dict['updated_at'])

        vn_obj.display_name = 'test-vn-timestamps'
        self._vnc_lib.virtual_network_update(vn_obj)
        vn_dict_2 = self.read_resource('network', vn_obj.uuid)
        # verify created timestamp and updated timestamp are not same
        self.assertIsNot(vn_dict_2['created_at'], vn_dict_2['updated_at'])
    # end test_network_timestamps

    def test_subnet_timestamps(self):
        timestamp = datetime.utcnow().isoformat()
        vn_obj = vnc_api.VirtualNetwork(self.id())
        sn_id = str(uuid.uuid4())
        vn_obj.add_network_ipam(vnc_api.NetworkIpam(),
            vnc_api.VnSubnetsType(
                [vnc_api.IpamSubnetType(vnc_api.SubnetType('1.1.1.0', 28),
                                        subnet_uuid=sn_id, created=timestamp,
                                        last_modified=timestamp)]))
        self._vnc_lib.virtual_network_create(vn_obj)
        sn_dict = self.read_resource('subnet', sn_id)
        # verify created timestamp and updated timestamp are same
        self.assertEqual(sn_dict['created_at'], sn_dict['updated_at'])

        proj_id = self._vnc_lib.fq_name_to_id('project',
        fq_name=['default-domain', 'default-project'])
        sn_dict_2 = self.update_resource('subnet', sn_id, proj_id,
                                       extra_res_fields={'name':
                                             'test-subnet-timestamps'})
        # verify created timestamp and updated timestamp are not same
        self.assertIsNot(sn_dict_2['created_at'], sn_dict_2['updated_at'])
    # end test_subnet_timestamps

    def test_external_network_perms(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain',
                                                       'default-project'])
        net_q = self.create_resource('network', proj_obj.uuid,
            extra_res_fields={'router:external': True})
        self.create_resource('subnet', proj_obj.uuid, extra_res_fields={
                'network_id': net_q['id'],
                'cidr': '1.1.1.0/24',
                'ip_version': 4,
        })

        net_obj = self._vnc_lib.virtual_network_read(net_q['contrail:fq_name'])
        self.assertEqual(net_obj.perms2.global_access, PERMS_RX)

        self.update_resource('network', net_q['id'], proj_obj.uuid,
                             extra_res_fields={'router:external':False})
        net_obj = self._vnc_lib.virtual_network_read(net_q['contrail:fq_name'])
        self.assertEqual(net_obj.perms2.global_access, PERMS_NONE)

        self.update_resource('network', net_q['id'], proj_obj.uuid,
                             extra_res_fields={'router:external':True})
        net_obj = self._vnc_lib.virtual_network_read(net_q['contrail:fq_name'])
        self.assertEqual(net_obj.perms2.global_access, PERMS_RX)
        self.delete_resource('network', proj_obj.uuid, net_q['id'])
    # end test_external_network_perms

    def test_external_network_fip_pool(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain',
                                                       'default-project'])
        net_q = self.create_resource('network', proj_obj.uuid,
            extra_res_fields={'router:external': True, 'shared': True})
        self.create_resource('subnet', proj_obj.uuid, extra_res_fields={
                'network_id': net_q['id'],
                'cidr': '1.1.1.0/24',
                'ip_version': 4,
        })

        fip_pool_fq_name = net_q['contrail:fq_name'] + ['floating-ip-pool']
        fip_pool_obj = self._vnc_lib.floating_ip_pool_read(
            fq_name=fip_pool_fq_name)
        self.assertEqual(fip_pool_obj.perms2.global_access, PERMS_RWX)
        self.delete_resource('network', proj_obj.uuid, net_q['id'])

        net_q = self.create_resource('network', proj_obj.uuid,
            extra_res_fields={'router:external': True, 'shared': False})
        self.create_resource('subnet', proj_obj.uuid, extra_res_fields={
                'network_id': net_q['id'],
                'cidr': '1.1.1.0/24',
                'ip_version': 4,
        })

        fip_pool_obj = self._vnc_lib.floating_ip_pool_read(
            fq_name=fip_pool_fq_name)
        self.assertEqual(fip_pool_obj.perms2.global_access, PERMS_RWX)
        self.delete_resource('network', proj_obj.uuid, net_q['id'])
    # end test_external_network_fip_pool

    def test_list_router_gw_interfaces_with_not_owned_public_network(self):
        # Admin project
        admin_proj_id = str(uuid.uuid4())
        admin_proj_name = 'admin-proj-%s' % self.id()
        test_case.get_keystone_client().tenants.add_tenant(admin_proj_id,
                                                           admin_proj_name)
        admin_proj_obj = self._vnc_lib.project_read(id=admin_proj_id)

        # Classic project
        proj_id = str(uuid.uuid4())
        proj_name = 'proj-%s' % self.id()
        test_case.get_keystone_client().tenants.add_tenant(proj_id, proj_name)
        proj_obj = self._vnc_lib.project_read(id=proj_id)

        # public network/subnet on an admin project
        public_net = self.create_resource(
            'network', admin_proj_obj.uuid, 'public-%s' % self.id(),
            extra_res_fields={'router:external': True},
        )
        self.create_resource(
            'subnet', admin_proj_obj.uuid, 'public-%s' % self.id(),
            extra_res_fields={
                'network_id': public_net['id'],
                'cidr': '80.0.0.0/24',
                'ip_version': 4,
            },
        )
        public_net_obj = self._vnc_lib.virtual_network_read(
            id=public_net['id'])

        # private network/subnet on classic project
        privat_net = self.create_resource('network', proj_obj.uuid,
                                          name='private-%s' % self.id())
        private_subnet = self.create_resource(
            'subnet',
            proj_obj.uuid,
            'private-%s' % self.id(),
            extra_res_fields={
                'network_id': privat_net['id'],
                'cidr': '10.0.0.0/24',
                'ip_version': 4,
            },
        )

        # Router on classic project with a gateway on the public network
        # (owned by the admin project)
        router = self.create_resource(
            'router',
            proj_obj.uuid,
            name='router-%s' % self.id(),
            extra_res_fields={
                'external_gateway_info': {
                    'network_id': public_net['id'],
                }
            },
        )

        # Add router interface on the private network/subnet
        self.update_resource(
            'router',
            router['id'],
            proj_obj.uuid,
            is_admin=True,
            operation='ADDINTERFACE',
            extra_res_fields={'subnet_id': private_subnet['id']},
        )
        router_obj = self._vnc_lib.logical_router_read(id=router['id'])

        # Create fake gw VMI
        fake_gw = vnc_api.VirtualMachineInterface(
            'fake-gw-interface-%s' % self.id(), proj_obj)
        fake_gw.add_virtual_network(public_net_obj)
        fake_gw.set_virtual_machine_interface_properties(
            vnc_api.VirtualMachineInterfacePropertiesType(
                service_interface_type='right',
            ),
        )
        fake_gw_id = self._vnc_lib.virtual_machine_interface_create(fake_gw)
        fake_gw = self._vnc_lib.virtual_machine_interface_read(id=fake_gw_id)
        mock.patch.object(fake_gw, 'get_instance_ip_back_refs',
                          return_value='fake_iip').start()

        def router_with_fake_si_ref(orig_method, *args, **kwargs):
            if 'obj_uuids' in kwargs and kwargs['obj_uuids'] == [router['id']]:
                mock_router_vn_ref = mock.patch.object(
                    router_obj, 'get_virtual_network_refs',).start()
                mock_router_vn_ref.return_value = [{
                    'uuid': public_net_obj.uuid}]
                mock_router_si_ref = mock.patch.object(
                    router_obj,
                    'get_service_instance_refs',
                ).start()
                mock_router_si_ref.return_value = [{'uuid': 'fake_si_uuid'}]
                return [router_obj]
            return orig_method(*args, **kwargs)

        def fake_si_obj(orig_method, *args, **kwargs):
            if 'id' in kwargs and kwargs['id'] == 'fake_si_uuid':
                si = mock.Mock()
                si.get_virtual_machine_back_refs.return_value = \
                    [{'to': 'fake_vm_name', 'uuid': 'fake_vm_uuid'}]
                return si
            return orig_method(*args, **kwargs)

        def return_router_gw_interface(orig_method, *args, **kwargs):
            if ('back_ref_id' in kwargs and
                    kwargs['back_ref_id'] == ['fake_vm_uuid']):
                return [fake_gw]
            return orig_method(*args, **kwargs)

        neutron_api_obj = FakeExtensionManager.get_extension_objects(
            'vnc_cfg_api.neutronApi')[0]
        neutron_db_obj = neutron_api_obj._npi._cfgdb
        with test_common.patch(neutron_db_obj._vnc_lib, 'logical_routers_list',
                               router_with_fake_si_ref), \
                test_common.patch(neutron_db_obj._vnc_lib,
                                  'service_instance_read', fake_si_obj), \
                test_common.patch(neutron_db_obj._vnc_lib,
                                  'virtual_machine_interfaces_list',
                                  return_router_gw_interface):
                # list with a user that not own the router gw port's network
                router_interfaces = self.list_resource(
                    'port',
                    proj_uuid=proj_obj.uuid,
                    req_filters={'device_id': [router['id']]},
                )
                self.assertEqual(len(router_interfaces), 1)
                # list as admin, project does not matter
                router_interfaces = self.list_resource(
                    'port',
                    proj_uuid=proj_obj.uuid,
                    req_filters={'device_id': [router['id']]},
                    is_admin=True,
                )
                self.assertEqual(len(router_interfaces), 2)

    def test_list_router_gw_interfaces_with_owned_public_network(self):
        # Admin project
        admin_proj_id = str(uuid.uuid4())
        admin_proj_name = 'admin-proj-%s' % self.id()
        test_case.get_keystone_client().tenants.add_tenant(admin_proj_id,
                                                           admin_proj_name)
        admin_proj_obj = self._vnc_lib.project_read(id=admin_proj_id)

        # public network/subnet
        public_net = self.create_resource(
            'network', admin_proj_obj.uuid, 'public-%s' % self.id(),
            extra_res_fields={'router:external': True},
        )
        self.create_resource(
            'subnet', admin_proj_obj.uuid, 'public-%s' % self.id(),
            extra_res_fields={
                'network_id': public_net['id'],
                'cidr': '80.0.0.0/24',
                'ip_version': 4,
            },
        )
        public_net_obj = self._vnc_lib.virtual_network_read(
            id=public_net['id'])

        # private network/subnet
        privat_net = self.create_resource('network', admin_proj_obj.uuid,
                                          name='private-%s' % self.id())
        private_subnet = self.create_resource(
            'subnet', admin_proj_obj.uuid, 'private-%s' % self.id(),
            extra_res_fields={
                'network_id': privat_net['id'],
                'cidr': '10.0.0.0/24',
                'ip_version': 4,
            },
        )

        # Router on admin project with a gateway on the public network
        # (owned by the admin project)
        router = self.create_resource(
            'router', admin_proj_obj.uuid, name='router-%s' % self.id(),
            extra_res_fields={
                'external_gateway_info': {
                    'network_id': public_net['id'],
                }
            },
        )

        # Add router interface on the private network/subnet
        self.update_resource(
            'router', router['id'], admin_proj_obj.uuid, is_admin=True,
            operation='ADDINTERFACE',
            extra_res_fields={'subnet_id': private_subnet['id']},
        )
        router_obj = self._vnc_lib.logical_router_read(id=router['id'])

        # Create fake gw VMI
        fake_gw = vnc_api.VirtualMachineInterface(
            'fake-gw-interface-%s' % self.id(), admin_proj_obj)
        fake_gw.add_virtual_network(public_net_obj)
        fake_gw.set_virtual_machine_interface_properties(
            vnc_api.VirtualMachineInterfacePropertiesType(
                service_interface_type='right',
            ),
        )
        fake_gw_id = self._vnc_lib.virtual_machine_interface_create(fake_gw)
        fake_gw = self._vnc_lib.virtual_machine_interface_read(id=fake_gw_id)
        mock.patch.object(fake_gw, 'get_instance_ip_back_refs',
                          return_value='fake_iip').start()

        def router_with_fake_si_ref(orig_method, *args, **kwargs):
            if 'obj_uuids' in kwargs and kwargs['obj_uuids'] == [router['id']]:
                mock_router_vn_ref = mock.patch.object(
                    router_obj, 'get_virtual_network_refs',).start()
                mock_router_vn_ref.return_value = [{
                    'uuid': public_net_obj.uuid}]
                mock_router_si_ref = mock.patch.object(
                    router_obj, 'get_service_instance_refs',).start()
                mock_router_si_ref.return_value = [{'uuid': 'fake_si_uuid'}]
                return [router_obj]
            return orig_method(*args, **kwargs)

        def fake_si_obj(orig_method, *args, **kwargs):
            if 'id' in kwargs and kwargs['id'] == 'fake_si_uuid':
                si = mock.Mock()
                si.get_virtual_machine_back_refs.return_value = \
                    [{'to': 'fake_vm_name', 'uuid': 'fake_vm_uuid'}]
                return si
            return orig_method(*args, **kwargs)

        def return_router_gw_interface(orig_method, *args, **kwargs):
            if ('back_ref_id' in kwargs and
                    kwargs['back_ref_id'] == ['fake_vm_uuid']):
                return [fake_gw]
            return orig_method(*args, **kwargs)

        neutron_api_obj = FakeExtensionManager.get_extension_objects(
            'vnc_cfg_api.neutronApi')[0]
        neutron_db_obj = neutron_api_obj._npi._cfgdb
        with test_common.patch(neutron_db_obj._vnc_lib, 'logical_routers_list',
                               router_with_fake_si_ref), \
                test_common.patch(neutron_db_obj._vnc_lib,
                                  'service_instance_read', fake_si_obj), \
                test_common.patch(neutron_db_obj._vnc_lib,
                                  'virtual_machine_interfaces_list',
                                  return_router_gw_interface):
                # list as admin, project does not matter
                router_interfaces = self.list_resource(
                    'port',
                    proj_uuid=admin_proj_obj.uuid,
                    req_filters={'device_id': [router['id']]},
                    is_admin=True,
                )
                self.assertEqual(len(router_interfaces), 2)
                # list as user owner of the router gw port's network
                router_interfaces = self.list_resource(
                    'port',
                    proj_uuid=admin_proj_obj.uuid,
                    req_filters={'device_id': [router['id']]},
                    is_admin=False,
                )
                self.assertEqual(len(router_interfaces), 2)

    def test_fip_owner(self):
        admin_proj_obj = vnc_api.Project('admin-proj-%s' %(self.id()), vnc_api.Domain())
        self._vnc_lib.project_create(admin_proj_obj)
        admin_proj_id = admin_proj_obj.uuid
        # external network
        net_q = self.create_resource('network', admin_proj_id,
                                      extra_res_fields={'router:external':True,
                                                        'shared': True})
        subnet_q = self.create_resource('subnet', admin_proj_id,
                                         extra_res_fields=
                                         {'network_id': net_q['id'],
                                          'cidr': '10.2.0.0/24',
                                          'ip_version': 4})

        proj_obj = vnc_api.Project('proj-%s' %(self.id()), vnc_api.Domain())
        self._vnc_lib.project_create(proj_obj)
        proj_id = proj_obj.uuid

        pvt_net_q = self.create_resource(
            'network', proj_id,
            extra_res_fields={'port_security_enabled': True},
        )
        pvt_subnet_q = self.create_resource('subnet', proj_id,
                                             extra_res_fields=
                                             {'network_id': pvt_net_q['id'],
                                              'cidr': '20.1.0.0/24',
                                              'ip_version': 4})

        sg_q = self.create_resource('security_group', proj_id)
        port_q = self.create_resource('port', proj_id,
                                       extra_res_fields=
                                       {'network_id':
                                        pvt_subnet_q['network_id'],
                                        'security_groups': [sg_q['id']]})

        fip_q = self.create_resource('floatingip', proj_id,
                                      extra_res_fields=
                                      {'floating_network_id': net_q['id'],
                                       'port_id': port_q['id']})

        fip = self._vnc_lib.floating_ip_read(id=fip_q['id'])

        self.assertEqual(fip.perms2.owner, proj_id)

    def test_update_any_other_fields_in_fip_doesnt_disassociate(self):
        proj_obj = vnc_api.Project('proj-%s' %(self.id()), vnc_api.Domain())
        self._vnc_lib.project_create(proj_obj)
        proj_id = proj_obj.uuid
        # external network
        net_q = self.create_resource('network', proj_id,
                                      extra_res_fields={'router:external':True})
        subnet_q = self.create_resource('subnet', proj_id,
                                         extra_res_fields=
                                         {'network_id': net_q['id'],
                                          'cidr': '10.2.0.0/24',
                                          'ip_version': 4})

        # private network
        pvt_net_q = self.create_resource(
            'network', proj_id,
            extra_res_fields={'port_security_enabled': True},
        )
        pvt_subnet_q = self.create_resource('subnet', proj_id,
                                             extra_res_fields=
                                             {'network_id': pvt_net_q['id'],
                                              'cidr': '20.1.0.0/24',
                                              'ip_version': 4})

        sg_q = self.create_resource('security_group', proj_id)
        port_q = self.create_resource('port', proj_id,
                                       extra_res_fields=
                                       {'network_id':
                                        pvt_subnet_q['network_id'],
                                        'security_groups': [sg_q['id']]})

        fip_q = self.create_resource('floatingip', proj_id,
                                      extra_res_fields=
                                      {'floating_network_id': net_q['id'],
                                       'port_id': port_q['id']})

        # Updating description
        self.update_resource('floatingip', fip_q['id'], proj_id,
                             extra_res_fields={'description':'test-fip'})
        fip_dict = self.read_resource('floatingip', fip_q['id'])
        self.assertEqual(fip_dict['port_id'], port_q['id'])

        # Disassociate fip from port
        self.update_resource('floatingip', fip_q['id'], proj_id,
                             extra_res_fields={'port_id': []})
        fip_dict = self.read_resource('floatingip', fip_q['id'])
        self.assertEqual(fip_dict['port_id'], None)

        # Associate fip to port
        self.update_resource('floatingip', fip_q['id'], proj_id,
                             extra_res_fields={'port_id': port_q['id']})
        fip_dict = self.read_resource('floatingip', fip_q['id'])
        self.assertEqual(fip_dict['port_id'], port_q['id'])

        # cleanup
        self.delete_resource('floatingip', proj_id, fip_q['id'])
        self.delete_resource('port', proj_id, port_q['id'])
        self.delete_resource('subnet', proj_id, subnet_q['id'])
        self.delete_resource('subnet', proj_id, pvt_subnet_q['id'])
        self.delete_resource('security_group', proj_id, sg_q['id'])
        self.delete_resource('network', proj_id, net_q['id'])
        self.delete_resource('network', proj_id, pvt_net_q['id'])
    # end test_update_any_other_fields_in_fip_doesnt_disassociate

    def test_dpdk_compute_port_bindings(self):
        vn_obj = vnc_api.VirtualNetwork(self.id())
        vn_obj.add_network_ipam(vnc_api.NetworkIpam(),
            vnc_api.VnSubnetsType(
                [vnc_api.IpamSubnetType(
                         vnc_api.SubnetType('192.168.10.0', 24))]))
        self._vnc_lib.virtual_network_create(vn_obj)

        vr_obj = vnc_api.VirtualRouter("dpdk-host")
        vr_obj.set_virtual_router_dpdk_enabled(True)
        vnc_vr_obj = self._vnc_lib.virtual_router_create(vr_obj)

        sg_obj = vnc_api.SecurityGroup('default')
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except vnc_api.RefsExistError:
            pass

        proj_uuid = self._vnc_lib.fq_name_to_id('project',
            fq_name=['default-domain', 'default-project'])

        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': True,
                   'roles': ''}
        data = {'resource':{'network_id': vn_obj.uuid,
                            'tenant_id': proj_uuid,
                            'binding:host_id': 'dpdk-host'}}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/port', body)
        port_dict = json.loads(resp.text)
        self.assertNotEqual(
            port_dict['binding:vif_details'].get('vhostuser_socket'),
            None)

        # disable dpdk on compute and we should observe the vif
        # details deleted from port bindings

        vr_obj.set_virtual_router_dpdk_enabled("")
        vnc_vr_obj = self._vnc_lib.virtual_router_update(vr_obj)

        resp = self._api_svr_app.post_json('/neutron/port', body)
        port_dict1 = json.loads(resp.text)
        self.assertEqual(
            port_dict1['binding:vif_details'].get('vhostuser_socket'),
            None)

        self.assertNotEqual(port_dict, port_dict1)
    # end test_port_bindings

    def test_non_dpdk_compute_port_bindings(self):
        vn_obj = vnc_api.VirtualNetwork(self.id())
        vn_obj.add_network_ipam(vnc_api.NetworkIpam(),
            vnc_api.VnSubnetsType(
                [vnc_api.IpamSubnetType(
                         vnc_api.SubnetType('192.168.11.0', 24))]))
        self._vnc_lib.virtual_network_create(vn_obj)

        vr_obj = vnc_api.VirtualRouter("non-dpdk-host")
        vnc_vr_obj = self._vnc_lib.virtual_router_create(vr_obj)

        sg_obj = vnc_api.SecurityGroup('default')
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except vnc_api.RefsExistError:
            pass

        proj_uuid = self._vnc_lib.fq_name_to_id('project',
            fq_name=['default-domain', 'default-project'])

        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': True,
                   'roles': ''}
        data = {'resource':{'network_id': vn_obj.uuid,
                            'tenant_id': proj_uuid,
                            'binding:host_id': 'non-dpdk-host'}}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/port', body)
        port_dict = json.loads(resp.text)
        self.assertEqual(
            port_dict['binding:vif_details'].get('vhostuser_socket'),
            None)

        # Enable dpdk on compute and we should observe the vif
        # details deleted from port bindings

        vr_obj.set_virtual_router_dpdk_enabled(True)
        vnc_vr_obj = self._vnc_lib.virtual_router_update(vr_obj)

        resp = self._api_svr_app.post_json('/neutron/port', body)
        port_dict1 = json.loads(resp.text)
        self.assertNotEqual(
            port_dict1['binding:vif_details'].get('vhostuser_socket'),
            None)

        self.assertNotEqual(port_dict, port_dict1)
    # end test_port_bindings

    def test_port_status(self):
        proj_obj = vnc_api.Project('proj-%s_new' %(self.id()), vnc_api.Domain())
        self._vnc_lib.project_create(proj_obj)

        vn = vnc_api.VirtualNetwork('vn-%s' % (self.id()), proj_obj)
        self._vnc_lib.virtual_network_create(vn)

        vm_name = 'virtual_machine1'
        vm = vnc_api.VirtualMachine(vm_name)
        self._vnc_lib.virtual_machine_create(vm)

        # First, create a VMI 'vmi1' which has a VM attached to it.
        # The status will be internally set to 'ACTIVE' for this port.
        vmi_name = 'virtual_machine_interface_1'
        vmi_obj = vnc_api.VirtualMachineInterface(vmi_name, proj_obj)
        vmi_obj.set_virtual_network(vn)
        vmi_obj.add_virtual_machine(vm)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        # Next, create a VMI 'vmi2' which has no VM attached to it.
        # The status will be internally set to 'DOWN' for this port.
        vmi_name = 'virtual_machine_interface_2'
        vmi_obj_2 = vnc_api.VirtualMachineInterface(vmi_name, proj_obj)
        vmi_obj_2.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj_2)

        list_result = self.list_resource(
            'port',
            proj_obj.uuid,
            req_filters={
                'status': 'ACTIVE',
            },
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['device_id'], 'virtual_machine1')

        list_result = self.list_resource(
            'port',
            proj_obj.uuid,
            req_filters={
                'status': 'DOWN',
            },
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['device_id'], '')

        # Clean the resources
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_obj_2.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm.uuid)
        self._vnc_lib.project_delete(id=proj_obj.uuid)
        # end test_port_status
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


class TestListWithFilters(test_case.NeutronBackendTestCase):

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

    def test_filters_with_id(self):
        neutron_api_obj = FakeExtensionManager.get_extension_objects(
            'vnc_cfg_api.neutronApi')[0]
        neutron_api_obj._npi._connect_to_db()
        neutron_db_obj = neutron_api_obj._npi._cfgdb

        # sg setup
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        sg_obj = vnc_api.SecurityGroup('sg-%s' %(self.id()), proj_obj)
        self._vnc_lib.security_group_create(sg_obj)

        # fip setup
        vn_obj = vnc_api.VirtualNetwork('vn1-%s' %(self.id()), proj_obj)
        vn_obj.add_network_ipam(vnc_api.NetworkIpam(),
            vnc_api.VnSubnetsType(
                [vnc_api.IpamSubnetType(vnc_api.SubnetType('1.1.1.0', 28))]))
        self._vnc_lib.virtual_network_create(vn_obj)
        fip_pool_obj = vnc_api.FloatingIpPool('fip-pool-%s' %(self.id()),
                                               vn_obj)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        fip1_obj = vnc_api.FloatingIp('fip1-%s' %(self.id()), fip_pool_obj)
        fip1_obj.add_project(proj_obj)
        self._vnc_lib.floating_ip_create(fip1_obj)

        proj2_obj = vnc_api.Project('proj2-%s' %(self.id()), vnc_api.Domain())
        self._vnc_lib.project_create(proj2_obj)
        fip2_obj = vnc_api.FloatingIp('fip2-%s' %(self.id()), fip_pool_obj)
        fip2_obj.add_project(proj2_obj)
        self._vnc_lib.floating_ip_create(fip2_obj)

        vmi_obj = vnc_api.VirtualMachineInterface(
                      'vmi-%s' %(self.id()), proj_obj)
        vmi_obj.add_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        fip3_obj = vnc_api.FloatingIp('fip3-%s' %(self.id()), fip_pool_obj)
        fip3_obj.add_virtual_machine_interface(vmi_obj)
        fip3_obj.add_project(proj_obj)
        self._vnc_lib.floating_ip_create(fip3_obj)

        def spy_list(orig_method, *args, **kwargs):
            self.assertIn(sg_obj.uuid, kwargs['obj_uuids'])
            return orig_method(*args, **kwargs)
        with test_common.patch(
            neutron_db_obj._vnc_lib, 'security_groups_list', spy_list):
            context = {'operation': 'READALL',
                       'user_id': '',
                       'tenant_id': proj_obj.uuid,
                       'roles': '',
                       'is_admin': 'False'}
            data = {'filters': {'id':[sg_obj.uuid]}}
            body = {'context': context, 'data': data}
            resp = self._api_svr_app.post_json(
                '/neutron/security_group', body)

        sg_neutron_list = json.loads(resp.text)
        self.assertEqual(len(sg_neutron_list), 1)
        self.assertEqual(sg_neutron_list[0]['id'], sg_obj.uuid)

        def spy_list(orig_method, *args, **kwargs):
            self.assertIn(fip1_obj.uuid, kwargs['obj_uuids'])
            return orig_method(*args, **kwargs)
        with test_common.patch(
            neutron_db_obj._vnc_lib, 'floating_ips_list', spy_list):
            context = {'operation': 'READALL',
                       'user_id': '',
                       'tenant_id': '',
                       'roles': '',
                       'is_admin': 'False'}
            # ask for one explicit id
            data = {'filters': {'id':[fip1_obj.uuid]}}
            body = {'context': context, 'data': data}
            resp = self._api_svr_app.post_json(
                '/neutron/floatingip', body)
            fip_neutron_list = json.loads(resp.text)
            self.assertEqual(len(fip_neutron_list), 1)
            self.assertEqual(fip_neutron_list[0]['id'], fip1_obj.uuid)

            # ask for list of ids AND in a project, should return 1
            data = {'filters': {'id':[fip1_obj.uuid, fip2_obj.uuid],
                                'tenant_id': [proj2_obj.uuid.replace('-', '')]}}
            body = {'context': context, 'data': data}
            resp = self._api_svr_app.post_json(
                '/neutron/floatingip', body)
            fip_neutron_list = json.loads(resp.text)
            self.assertEqual(len(fip_neutron_list), 1)
            self.assertEqual(fip_neutron_list[0]['id'], fip2_obj.uuid)

            # ask for list of ids AND on a VMI, should return 1
            data = {'filters': {'id':[fip1_obj.uuid, fip2_obj.uuid,
                                      fip3_obj.uuid],
                                'tenant_id': [proj2_obj.uuid.replace('-', '')]}}
            body = {'context': context, 'data': data}
            resp = self._api_svr_app.post_json(
                '/neutron/floatingip', body)
            fip_neutron_list = json.loads(resp.text)
            self.assertEqual(len(fip_neutron_list), 1)
            self.assertEqual(fip_neutron_list[0]['id'], fip2_obj.uuid)

        self._vnc_lib.security_group_delete(id=sg_obj.uuid)
        self._vnc_lib.floating_ip_delete(id=fip1_obj.uuid)
        self._vnc_lib.floating_ip_delete(id=fip2_obj.uuid)
        self._vnc_lib.floating_ip_delete(id=fip3_obj.uuid)
        self._vnc_lib.floating_ip_pool_delete(id=fip_pool_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)
        self._vnc_lib.project_delete(id=proj2_obj.uuid)
    # end test_filters_with_id

    def test_filters_with_shared_and_router_external(self):
        proj_obj = vnc_api.Project('proj-%s' %(self.id()), vnc_api.Domain())
        self._vnc_lib.project_create(proj_obj)

        vn1_obj = vnc_api.VirtualNetwork('vn1-%s' %(self.id()), proj_obj)
        vn1_obj.set_is_shared(False)
        self._vnc_lib.virtual_network_create(vn1_obj)

        vn2_obj = vnc_api.VirtualNetwork('vn2-%s' %(self.id()), proj_obj)
        vn2_obj.set_router_external(False)
        self._vnc_lib.virtual_network_create(vn2_obj)

        vn3_obj = vnc_api.VirtualNetwork('vn3-%s' %(self.id()), proj_obj)
        vn3_obj.set_is_shared(False)
        vn3_obj.set_router_external(True)
        self._vnc_lib.virtual_network_create(vn3_obj)

        #filter for list of shared network='False' should return 2
        vn1_neutron_list = self.list_resource(
                                'network', proj_uuid=proj_obj.uuid,
                                req_filters={'shared': [False]})
        vn1_3_subnet_list = self.list_resource(
                                 'subnet', proj_uuid=proj_obj.uuid,
                                 req_filters={'shared': [False]})
        # shared = False should return all the networks that belongs to
        # the tenant which will have either is_shared = False or None
        self.assertEqual(len(vn1_neutron_list), 3)
        self.assertEqual(len(vn1_3_subnet_list), 3)
        vn_ids = []
        vn_ids.append(vn1_neutron_list[0]['id'])
        vn_ids.append(vn1_neutron_list[1]['id'])
        vn_ids.append(vn1_neutron_list[2]['id'])
        self.assertIn(vn1_obj.uuid, vn_ids)
        self.assertIn(vn2_obj.uuid, vn_ids)
        self.assertIn(vn3_obj.uuid, vn_ids)

        #filter for list of router:external='False' network should return 1
        vn2_neutron_list = self.list_resource(
                                'network', proj_uuid=proj_obj.uuid,
                                req_filters={'router:external': [False]})
        self.assertEqual(len(vn2_neutron_list), 1)
        self.assertEqual(vn2_neutron_list[0]['id'], vn2_obj.uuid)

        #filter for list of router:external and
        #shared network='False' should return 1
        vn3_neutron_list = self.list_resource(
                                'network', proj_uuid=proj_obj.uuid,
                                req_filters={'shared': [False],
                                             'router:external': [True]})
        self.assertEqual(len(vn3_neutron_list), 1)
        self.assertEqual(vn3_neutron_list[0]['id'], vn3_obj.uuid)


        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn3_obj.uuid)
    # end test_filters_with_shared_and_router_external
# end class TestListWithFilters


class TestAuthenticatedAccess(test_case.NeutronBackendTestCase):
    test_obj_uuid = None
    test_failures = []
    expected_auth_token = ''
    @classmethod
    def setUpClass(cls):
        from keystonemiddleware import auth_token
        class FakeAuthProtocol(object):
            _test_cls = cls
            def __init__(self, app, *args, **kwargs):
                self._app = app
            # end __init__
            def __call__(self, env, start_response):
                # in multi-tenancy mode only admin role admitted
                # by api-server till full rbac support
                if (env['REQUEST_METHOD'] == 'GET' and
                    env['PATH_INFO'] == '/virtual-network/%s' %(
                        self._test_cls.test_obj_uuid)):
                    # always execute but return back assertion
                    # errors
                    if not 'HTTP_X_AUTH_TOKEN' in env:
                        self._test_cls.test_failures.append(
                            'Missing HTTP_X_AUTH_TOKEN')
                    if not env['HTTP_X_AUTH_TOKEN'].startswith(
                        self._test_cls.expected_auth_token):
                        self._test_cls.test_failures.append(
                            'Found wrong HTTP_X_AUTH_TOKEN %s' %(
                            env['HTTP_X_AUTH_TOKEN']))
                    env['HTTP_X_ROLE'] = 'admin'
                    return self._app(env, start_response)
                else:
                    env['HTTP_X_ROLE'] = 'admin'
                    return self._app(env, start_response)
            # end __call__
        # end class FakeAuthProtocol
        super(TestAuthenticatedAccess, cls).setUpClass(
            extra_config_knobs=[
                ('DEFAULTS', 'auth', 'keystone'),
                ('DEFAULTS', 'multi_tenancy', True),
                ('KEYSTONE', 'admin_user', 'foo'),
                ('KEYSTONE', 'admin_password', 'bar'),
                ('KEYSTONE', 'admin_tenant_name', 'baz'),],
            extra_mocks=[
                (auth_token, 'AuthProtocol', FakeAuthProtocol),
                ])
    # end setupClass

    def test_post_neutron_checks_auth_token(self):
        test_obj = self._create_test_object()
        TestAuthenticatedAccess.test_obj_uuid = test_obj.uuid
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': test_obj.uuid}
        body = {'context': context, 'data': data}
        TestAuthenticatedAccess.expected_auth_token = 'no user token for'
        self._api_svr_app.post_json('/neutron/network', body)
        self.assertEqual(self.test_failures, [])

        TestAuthenticatedAccess.expected_auth_token = 'abc123'
        self._api_svr_app.post_json('/neutron/network', body,
            headers={'X_AUTH_TOKEN':'abc123'})
        self.assertEqual(self.test_failures, [])
    # end test_post_neutron_checks_auth_token
# end class TestAuthenticatedAccess

class TestKeystoneCallCount(test_case.NeutronBackendTestCase):
    test_obj_uuid = None
    test_failures = []
    expected_auth_token = ''
    _api_session = requests.Session()
    adapter = requests.adapters.HTTPAdapter()
    _api_session.mount("http://", adapter)
    _call_count = 0
    @classmethod
    def setUpClass(cls):
        from keystonemiddleware import auth_token
        class FakeAuthProtocol(object):
            _test_cls = cls
            def __init__(self, app, *args, **kwargs):
                self._app = app

            # end __init__
            def __call__(self, env, start_response):
                #Count number of calls made
                if env.get('HTTP_X_AUTH_TOKEN') == 'abc123':
                    self._test_cls._call_count = self._test_cls._call_count + 1
                env['HTTP_X_ROLE'] = 'admin'
                return self._app(env, start_response)
            # end __call__
        # end class FakeAuthProtocol
        super(TestKeystoneCallCount, cls).setUpClass(
            extra_config_knobs=[
                ('DEFAULTS', 'auth', 'keystone'),
                ('DEFAULTS', 'aaa_mode', 'cloud-admin'),
                ('KEYSTONE', 'admin_user', 'foo'),
                ('KEYSTONE', 'admin_password', 'bar'),
                ('KEYSTONE', 'admin_tenant_name', 'baz'),],
            extra_mocks=[
                (auth_token, 'AuthProtocol', FakeAuthProtocol),
                ])
    # end setupClass

    def test_keystone_call_count(self):
        test_obj = self._create_test_object()
        context = {'operation': 'READ',
                   'user_id': '',
                   'roles': ''}
        data = {'fields': None,
                'id': test_obj.uuid}
        body = {'context': context, 'data': data}
        uri = '/neutron/network'
        url = "http://%s:%s%s" \
              % (self._vnc_lib._web_host, self._vnc_lib._web_port, uri)
        headers=self._vnc_lib._headers
        headers['X_AUTH_TOKEN'] = 'abc123'
        header = json.dumps(headers)
        body = json.dumps(body)
        # currently OP_GET goes through LocalVncApi.
        TestKeystoneCallCount._api_session.post(url, data=body, headers=headers, verify=False)
        self.assertEqual(TestKeystoneCallCount._call_count, 1)
    # end test_keystone_call_count
# end class TestKeystoneCallCount

class TestRBACPerms(test_case.NeutronBackendTestCase):
    domain_name = 'default-domain'
    fqdn = [domain_name]
    _api_session = requests.Session()
    adapter = requests.adapters.HTTPAdapter()
    _api_session.mount("http://", adapter)

    @classmethod
    def setUpClass(cls):
        from keystonemiddleware import auth_token
        class FakeAuthProtocol(object):
            _test_cls = cls
            def __init__(self, app, *args, **kwargs):
                self._app = app

            # end __init__
            def __call__(self, env, start_response):
                #Count number of calls made
                if env.get('HTTP_X_AUTH_TOKEN') == 'test123':
                    env['HTTP_X_ROLE'] = 'test'
                else:
                    env['HTTP_X_ROLE'] = 'cloud-admin'
                return self._app(env, start_response)
            # end __call__
        # end class FakeAuthProtocol

        extra_mocks = [(auth_token, 'AuthProtocol',
                            FakeAuthProtocol)]
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('DEFAULTS', 'global_read_only_role', 'read-only-role'),
            ('DEFAULTS', 'auth', 'keystone'),
        ]
        super(TestRBACPerms, cls).setUpClass(extra_mocks=extra_mocks,
            extra_config_knobs=extra_config_knobs)

    def test_neutron_perms(self):
        test_obj = self._create_test_object()
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': False,
                   'roles': '',
                   'tenant_id': proj_obj.uuid}

        data = {'resource': {'name': 'test_network',
                             'tenant_id': proj_obj.uuid}}
        body = {'context': context, 'data': data}
        uri = '/neutron/network'
        url = "http://%s:%s%s" \
              % (self._vnc_lib._web_host, self._vnc_lib._web_port, uri)
        headers=self._vnc_lib._headers
        headers['X_AUTH_TOKEN'] = 'test123'
        header = json.dumps(headers)
        body = json.dumps(body)
        test_pass = False
        val = TestRBACPerms._api_session.post(url, data=body, headers=headers, verify=False)
        self.assertIn('NotAuthorized', val._content)
