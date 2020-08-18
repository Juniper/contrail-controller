from __future__ import absolute_import

import logging

from testtools import ExpectedException
import webtest.app

from . import test_case

logger = logging.getLogger(__name__)


class TestStrictCompOn(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestStrictCompOn, cls).setUpClass(
            extra_config_knobs=[('NEUTRON', 'strict_compliance', True)])
    # end setUpClass

    def _create_floatingip_and_associate_port_without_ext_gw(self, proj_id):
        # external network
        net_q = self.create_resource(
            'network', proj_id, extra_res_fields={
                'router:external': True})
        subnet_q = self.create_resource(
            'subnet',
            proj_id,
            extra_res_fields={
                'network_id': net_q['id'],
                'cidr': '10.2.0.0/24',
                'ip_version': 4})

        # private network
        pvt_net_q = self.create_resource('network', proj_id)
        pvt_subnet_q = self.create_resource(
            'subnet',
            proj_id,
            extra_res_fields={
                'network_id': pvt_net_q['id'],
                'cidr': '10.1.0.0/24',
                'ip_version': 4})

        port_q = self.create_resource(
            'port', proj_id, extra_res_fields={
                'network_id': pvt_subnet_q['network_id']})

        return self.create_resource(
            'floatingip',
            proj_id,
            extra_res_fields={
                'floating_network_id': net_q['id'],
                'port_id': port_q['id']})

    def _create_floatingip_and_associate_port_with_ext_gw(self, proj_id):
        # external network
        net_q = self.create_resource(
            'network', proj_id, extra_res_fields={
                'router:external': True})
        subnet_q = self.create_resource(
            'subnet',
            proj_id,
            extra_res_fields={
                'network_id': net_q['id'],
                'cidr': '10.2.0.0/24',
                'ip_version': 4})
        router_q = self.create_resource('router', proj_id)

        # private network
        pvt_net_q = self.create_resource('network', proj_id)
        pvt_subnet_q = self.create_resource(
            'subnet',
            proj_id,
            extra_res_fields={
                'network_id': pvt_net_q['id'],
                'cidr': '10.1.0.0/24',
                'ip_version': 4})

        port_q = self.create_resource(
            'port', proj_id, extra_res_fields={
                'network_id': pvt_subnet_q['network_id']})
        port2_q = self.create_resource(
            'port', proj_id, extra_res_fields={
                'network_id': pvt_subnet_q['network_id']})

        # External gateway
        router_q = self.update_resource(
            'router', router_q['id'], proj_id, extra_res_fields={
                'external_gateway_info': {
                    'network_id': net_q['id']}})
        router_q = self.add_router_interface(
            router_q['id'], proj_id, extra_res_fields={
                'port_id': port2_q['id']})
        return self.create_resource(
            'floatingip',
            proj_id,
            extra_res_fields={
                'floating_network_id': net_q['id'],
                'port_id': port_q['id']})

    # test when strict_compliance is ON
    def test_create_fip_and_associate_port_without_ext_gw(self):
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        res_q = self.create_resource('security_group', proj_obj.uuid)
        self.list_resource(
            'security_group',
            proj_uuid=proj_obj.uuid,
            req_filters={
                'name': res_q['name']})

        with ExpectedException(webtest.app.AppError):
            self._create_floatingip_and_associate_port_without_ext_gw(
                proj_obj.uuid)

    # test when strict_compliance is ON
    def test_create_fip_and_associate_port_with_ext_gw(self):
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        res_q = self.create_resource('security_group', proj_obj.uuid)
        self.list_resource(
            'security_group',
            proj_uuid=proj_obj.uuid,
            req_filters={
                'name': res_q['name']})

        self._create_floatingip_and_associate_port_with_ext_gw(proj_obj.uuid)
# end class TestStrictCompON


class TestStrictCompOff(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestStrictCompOff, cls).setUpClass(
            extra_config_knobs=[('NEUTRON', 'strict_compliance', False)])
    # end setUpClass

    def _create_floatingip_and_associate_port_without_ext_gw(self, proj_id):
        # external network
        net_q = self.create_resource(
            'network', proj_id, extra_res_fields={
                'router:external': True})
        self.create_resource(
            'subnet',
            proj_id,
            extra_res_fields={
                'network_id': net_q['id'],
                'cidr': '10.2.0.0/24',
                'ip_version': 4})

        # private network
        pvt_net_q = self.create_resource('network', proj_id)
        pvt_subnet_q = self.create_resource(
            'subnet',
            proj_id,
            extra_res_fields={
                'network_id': pvt_net_q['id'],
                'cidr': '10.1.0.0/24',
                'ip_version': 4})

        port_q = self.create_resource(
            'port', proj_id, extra_res_fields={
                'network_id': pvt_subnet_q['network_id']})

        return self.create_resource(
            'floatingip',
            proj_id,
            extra_res_fields={
                'floating_network_id': net_q['id'],
                'port_id': port_q['id']})

    # test when strict_compliance is OFF
    def test_create_fip_and_associate_port_without_ext_gw(self):
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        res_q = self.create_resource('security_group', proj_obj.uuid)
        self.list_resource(
            'security_group',
            proj_uuid=proj_obj.uuid,
            req_filters={
                'name': res_q['name']})
        self._create_floatingip_and_associate_port_without_ext_gw(
            proj_obj.uuid)
# end class TestStrictCompOFF
