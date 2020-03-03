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

from vnc_cfg_api_server.tests import test_case


class TestFloatingIP(test_case.ApiServerTestCase):
    def test_get_floating_ip_project_ref(self):
        # create project
        project = Project(name='project-{}'.format(self.id()))
        p_uuid = self._vnc_lib.project_create(project)
        project.set_uuid(p_uuid)

        # create virtual network
        vn = VirtualNetwork(name='vn-{}'.format(self.id()),
                            parent_obj=project)
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

        # get floating IPs
        fip_list = self._vnc_lib.floating_ips_list(obj_uuids=[fip_uuid],
                                                   fields=['project_refs'],
                                                   detail=True)
        self.assertEqual(len(fip_list), 1)
        project_refs = fip_list[0].get_project_refs()
        self.assertEqual(len(project_refs), 1)

        self.assertEqual(project_refs[0]['uuid'], p_uuid)
        self.assertEqual(project_refs[0]['to'], project.fq_name)
