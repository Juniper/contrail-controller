import sys
sys.path.append("../common/tests")
from vnc_api import vnc_api
import cfgm_common.ifmap.client as ifmap_client
from test_utils import *
import test_common


class ApiServerTestCase(test_common.TestCase):
    def setUp(self):
        super(ApiServerTestCase, self).setUp()
        self.ignore_err_in_log = False

    def tearDown(self):
        try:
            if self.ignore_err_in_log:
                return

            with open('api_server_%s.log' %(self.id())) as f:
                lines = f.read()
                self.assertIsNone(
                    re.search('SYS_ERR', lines), 'Error in log file')
        except IOError:
            # vnc_openstack.err not created, No errors.
            pass
        finally:
            super(ApiServerTestCase, self).tearDown()

    def _create_vn_ri_vmi(self, obj_count=1):
        vn_objs = []
        ipam_objs = []
        ri_objs = []
        vmi_objs = []
        for i in range(obj_count):
            vn_obj = vnc_api.VirtualNetwork('%s-vn-%s' %(self.id(), i))
            vn_obj.set_virtual_network_network_id(i)

            ipam_obj = vnc_api.NetworkIpam('%s-ipam-%s' % (self.id(), i))
            vn_obj.add_network_ipam(ipam_obj, vnc_api.VnSubnetsType())
            self._vnc_lib.network_ipam_create(ipam_obj)
            ipam_objs.append(ipam_obj)

            self._vnc_lib.virtual_network_create(vn_obj)
            vn_objs.append(vn_obj)

            ri_obj = vnc_api.RoutingInstance('%s-ri-%s' %(self.id(), i),
                                     parent_obj=vn_obj)
            self._vnc_lib.routing_instance_create(ri_obj)
            ri_objs.append(ri_obj)

            vmi_obj = vnc_api.VirtualMachineInterface('%s-vmi-%s' %(self.id(), i),
                                              parent_obj=vnc_api.Project())
            vmi_obj.add_virtual_network(vn_obj)
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
            vmi_objs.append(vmi_obj)

        return vn_objs, ipam_objs, ri_objs, vmi_objs
    # end _create_vn_ri_vmi
# end class ApiServerTestCase


class ApiServerIrondTestCase(ApiServerTestCase):
    @classmethod
    def setUpClass(cls, extra_config_knobs=None, extra_mocks=None):
        irond_config = [
            ('DEFAULTS', 'ifmap_server_ip', '127.0.0.1'),
            ('DEFAULTS', 'ifmap_health_check_interval', '3600'),
        ]
        irond_mocks = [
            (ifmap_client.client, '__init__', FakeIfmapClient.initialize),
            (ifmap_client.client, 'call', FakeIfmapClient.call),
            (ifmap_client.client, 'call_async_result',
             FakeIfmapClient.call_async_result),
        ]
        if extra_config_knobs:
            extra_config_knobs.extend(irond_config)
        else:
            extra_config_knobs = irond_config
        if extra_mocks:
            extra_mocks.extend(irond_mocks)
        else:
            extra_mocks = irond_mocks
        super(ApiServerIrondTestCase, cls).setUpClass(
              extra_config_knobs=extra_config_knobs, extra_mocks=extra_mocks)
