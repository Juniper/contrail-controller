# Copyright 2019 Juniper Networks. All rights reserved.

import uuid
import json

from gevent import monkey
monkey.patch_all()  # noqa
from vnc_api import vnc_api as vnc_api

from tests import test_case

class TestTrunk(test_case.NeutronBackendTestCase):
    def setUp(self):
        super(TestTrunk, self).setUp()
        self.project_id = self._vnc_lib.project_create(
            vnc_api.Project('project-%s' % self.id()))
        self.project = self._vnc_lib.project_read(id=self.project_id)

    def _add_subports(self, project_id, trunk_id, port_id):
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
        self.assertEquals(trunk_dict['name'], trunk_read_dict['name'])
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
        sub_port.set_virtual_network(vn)
        sub_port_id = self._vnc_lib.virtual_machine_interface_create(sub_port)
        neutron_trunk = self._add_subports(self.project_id,
                                           trunk_dict['id'],
                                           sub_port_id)
        self.assertEquals(neutron_trunk['sub_ports'][0].get('uuid'),
                          sub_port.uuid)

        neutron_trunk = self._remove_subports(self.project_id,
                                           trunk_dict['id'],
                                           sub_port_id)
        self.assertEquals(neutron_trunk['sub_ports'], [])
        # Clean the resources
        self.delete_resource('trunk', self.project_id, trunk_dict['id'])
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        self._vnc_lib.virtual_machine_interface_delete(id=sub_port_id)
