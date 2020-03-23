# Copyright 2019 Juniper Networks. All rights reserved.

from builtins import str
from builtins import range
import uuid
import json
import re

from gevent import monkey
monkey.patch_all()  # noqa
from vnc_api import vnc_api as vnc_api
import webtest.app

from vnc_openstack.tests import test_case

class TestTrunk(test_case.NeutronBackendTestCase):

    VMI_NUM = 2

    def setUp(self):
        super(TestTrunk, self).setUp()
        self.project_id = self._vnc_lib.project_create(
            vnc_api.Project('project-%s' % self.id()))
        self.project = self._vnc_lib.project_read(id=self.project_id)

    def _add_subports(self, project_id, trunk_id, port_id, vlan_tag=None):
        extra_res_fields = {
            'sub_ports': [
                {
                    'port_id': port_id,
                }
            ]
        }
        if vlan_tag:
           extra_res_fields['sub_ports'][0]['segmentation_id'] = vlan_tag

        return self.update_resource(
            'trunk',
            trunk_id,
            project_id,
            extra_res_fields=extra_res_fields,
            operation='ADD_SUBPORTS')

    def _remove_subports(self, project_id, trunk_id, port_id):
        extra_res_fields = {
            'sub_ports': [
                {
                    'port_id': port_id,
                }
            ]
        }
        return self.update_resource(
            'trunk',
            trunk_id,
            project_id,
            extra_res_fields=extra_res_fields,
            operation='REMOVE_SUBPORTS')

    def test_trunk_crud(self):
        vn = vnc_api.VirtualNetwork('vn-%s' % (self.id()))
        self._vnc_lib.virtual_network_create(vn)
        vmi_obj = vnc_api.VirtualMachineInterface(
            'trunk-port',
            parent_obj=self.project)
        vmi_obj.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': True,
                   'roles': ''}
        data = {'resource':{'name': 'trunk-%s' % (self.id()),
                            'tenant_id': self.project_id,
                            'port_id': vmi_id}}
        body = {'context': context, 'data': data}
        # Create Trunk
        resp = self._api_svr_app.post_json('/neutron/trunk', body)
        trunk_dict = json.loads(resp.text)

        # Read Trunk
        trunk_read_dict = self.read_resource('trunk', trunk_dict['id'])
        self.assertEquals(trunk_dict['id'], trunk_read_dict['id'])

        # Update Trunk
        extra_res_fields = { 'name': 'trunk_update'}
        trunk_update_dict = self.update_resource(
            'trunk',
            trunk_dict['id'],
            self.project_id,
            extra_res_fields=extra_res_fields)

        self.assertEquals(trunk_update_dict['name'], extra_res_fields['name'])

        # Delete Trunk
        self.delete_resource('trunk', self.project_id, trunk_dict['id'])

        # Clean the resources
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

    def test_trunk_create_with_same_vlan_tag_negative(self):
        vn = vnc_api.VirtualNetwork('vn-same-vlan-tag-%s' % (self.id()))
        self._vnc_lib.virtual_network_create(vn)
        vmi_obj = vnc_api.VirtualMachineInterface(
            'trunk-port-same-vlan-tag',
            parent_obj=self.project)
        vmi_obj.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        sub_ports = []

        for i in range(self.VMI_NUM):
            sub_vmi_dict = {}
            sub_vmi_obj = vnc_api.VirtualMachineInterface(
                'sub-port-%s' %(i),
                parent_obj=self.project)
            sub_vmi_obj.set_virtual_network(vn)
            sub_port_id = self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj)
            sub_vmi_dict['port_id'] = sub_port_id
            sub_vmi_dict['segmentation_id'] = 10
            sub_ports.append(sub_vmi_dict)

        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': True,
                   'roles': ''}
        data = {'resource':{'name': 'trunk-%s' % (self.id()),
                            'tenant_id': self.project_id,
                            'port_id': vmi_id,
                            'sub_ports': sub_ports}}
        body = {'context': context, 'data': data}
        # Create Trunk should fail since sub ports have same vlan tag
        try:
            self._api_svr_app.post_json('/neutron/trunk', body)
        except webtest.app.AppError as e:
            self.assertIsNot(re.search('DuplicateSubPort', str(e)), None)

        # Clean the resources
        for i in range(self.VMI_NUM):
            self._vnc_lib.virtual_machine_interface_delete(
                                  id=sub_ports[i].get('port_id'))
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

    def test_trunk_add_and_delete_sub_ports(self):
        vn = vnc_api.VirtualNetwork('vn-%s' % (self.id()))
        self._vnc_lib.virtual_network_create(vn)
        vmi_obj = vnc_api.VirtualMachineInterface(
            'trunk-port',
            parent_obj=self.project)
        vmi_obj.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': True,
                   'roles': ''}
        data = {'resource':{'name': 'trunk-%s' % (self.id()),
                            'tenant_id': self.project_id,
                            'port_id': vmi_id}}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/trunk', body)
        trunk_dict = json.loads(resp.text)

        sub_port = vnc_api.VirtualMachineInterface(
            'trunk-sub-port-%s' % (self.id()), parent_obj=self.project)
        vmi_prop = vnc_api.VirtualMachineInterfacePropertiesType(
                     sub_interface_vlan_tag=10)
        sub_port.set_virtual_machine_interface_properties(vmi_prop)
        sub_port.set_virtual_network(vn)
        sub_port_id = self._vnc_lib.virtual_machine_interface_create(sub_port)

        neutron_trunk = self._add_subports(self.project_id,
                                           trunk_dict['id'],
                                           sub_port_id)
        self.assertEquals(neutron_trunk['sub_ports'][0].get('port_id'),
                          sub_port.uuid)

        sub_port_neg = vnc_api.VirtualMachineInterface(
            'trunk-sub-port-neg-%s' % (self.id()), parent_obj=self.project)
        sub_port_neg.set_virtual_network(vn)
        sub_port_id_neg = self._vnc_lib.virtual_machine_interface_create(sub_port_neg)
        # Adding sub port with the vlan tag that already exists in a trunk
        # should return an exception.
        try:
            self._add_subports(self.project_id,
                               trunk_dict['id'],
                               sub_port_id_neg,
                               vlan_tag=10)
        except webtest.app.AppError as e:
            self.assertIsNot(re.search('DuplicateSubPort', str(e)), None)

        neutron_trunk = self._remove_subports(self.project_id,
                                           trunk_dict['id'],
                                           sub_port_id)
        self.assertEquals(neutron_trunk['sub_ports'], [])
        # Clean the resources
        self.delete_resource('trunk', self.project_id, trunk_dict['id'])
        self._vnc_lib.virtual_machine_interface_delete(id=sub_port_id)
        self._vnc_lib.virtual_machine_interface_delete(id=sub_port_id_neg)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

    def test_list_trunks(self):
        vn = vnc_api.VirtualNetwork('%s-vn' % self.id(),
                                    parent_obj=self.project)
        self._vnc_lib.virtual_network_create(vn)
        neutron_trunks = []
        trunk_ids = []
        vmi_ids = []
        for i in range(2):
            vmi = vnc_api.VirtualMachineInterface(
                '%s-vmi%d' % (self.id(), i), parent_obj=self.project)
            vmi.add_virtual_network(vn)
            vmi_ids.append(self._vnc_lib.virtual_machine_interface_create(vmi))

            neutron_trunks.append(
                self.create_resource(
                    'trunk',
                    self.project_id,
                    extra_res_fields={
                        'name': '%s-trunk-%d' % (self.id(), i),
                        'tenant_id': self.project_id,
                        'port_id': vmi.uuid
                    },
                ),
            )
        list_result = self.list_resource('trunk', self.project_id)
        self.assertEquals(len(list_result), len(neutron_trunks))
        self.assertEquals({r['id'] for r in list_result},
                          {r['id'] for r in neutron_trunks})

        # Clean the resources
        for i in range(2):
            self.delete_resource('trunk',
                                 self.project_id,
                                 neutron_trunks[i]['id'])
            self._vnc_lib.virtual_machine_interface_delete(id=vmi_ids[i])
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

    def test_add_parent_port_to_another_trunk_negative(self):
        vn = vnc_api.VirtualNetwork('vn-%s' % (self.id()))
        self._vnc_lib.virtual_network_create(vn)
        vmi_obj = vnc_api.VirtualMachineInterface(
            'trunk-port',
            parent_obj=self.project)
        vmi_obj.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': True,
                   'roles': ''}
        data = {'resource':{'name': 'trunk-%s' % (self.id()),
                            'tenant_id': self.project_id,
                            'port_id': vmi_id}}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/trunk', body)
        trunk_dict = json.loads(resp.text)

        try:
            self.create_resource(
                        'trunk',
                        self.project_id,
                        extra_res_fields={
                            'name': 'trunk-negative-%s' % (self.id()),
                            'tenant_id': self.project_id,
                            'port_id': vmi_id
                        },
                ),
        except webtest.app.AppError as e:
            self.assertIsNot(re.search('TrunkPortInUse', str(e)), None)

        self.delete_resource('trunk', self.project_id, trunk_dict['id'])
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
