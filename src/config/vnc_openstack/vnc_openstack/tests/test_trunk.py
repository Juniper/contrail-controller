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

    def test_trunk_crud(self):
        vn = vnc_api.VirtualNetwork('vn-%s' % (self.id()))
        self._vnc_lib.virtual_network_create(vn)
        vmi_obj = vnc_api.VirtualMachineInterface('trunk-port', parent_obj=self.project)
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

        # Clean the resources
        self.delete_resource('trunk', self.project_id, trunk_dict['id'])
        self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
