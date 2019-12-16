from __future__ import absolute_import

from vnc_api.vnc_api import (
    FloatingIp, FloatingIpPool,
    IpamSubnetType,
    NetworkIpam,
    Project,
    SubnetType,
    VirtualNetwork,
    VnSubnetsType,
)
from . import test_case

try:
    from neutron_lib import constants
except ImportError:
    from neutron.common import constants


class TestFloatingIP(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestFloatingIP, cls).setUpClass(
            extra_config_knobs=[
                ('DEFAULTS', 'apply_subnet_host_routes', True)
            ])

    def test_get_floating_ip_project_ref(self):
        # create project
        project = Project(name='project-{}'.format(self.id()))
        p_uuid = self._vnc_lib.project_create(project)
        project.set_uuid(p_uuid)

        # create virtual network
        vn = VirtualNetwork(name='vn-{}'.format(self.id()))
        vn.add_network_ipam(NetworkIpam(), VnSubnetsType([
            IpamSubnetType(SubnetType('1.1.1.0', 28)),
        ]))
        vn_uuid = self._vnc_lib.virtual_network_create(vn)
        vn.set_uuid(vn_uuid)

        # create floating IP pool
        fip_pool = FloatingIpPool(name='fip_p-{}'.format(self.id()),
                                  parent_obj=vn)
        fip_p_uuid = self._vnc_lib.floating_ip_pool_create(fip_pool)
        fip_pool.set_uuid(fip_p_uuid)

        # finally, create floating IP
        fip = FloatingIp(name='fip-{}'.format(self.id()),
                         parent_obj=fip_pool)
        fip_uuid = self._vnc_lib.floating_ip_create(fip)
        fip.set_uuid(fip_uuid)

        # get floating IPs as neutron
        list_result = self.list_resource('floatingip', project.get_uuid(),
                                         is_admin=True)
        self.assertEqual(len(list_result), 1)

        # TODO(pawel.zadrozny): Check if floating ip has ref to project
