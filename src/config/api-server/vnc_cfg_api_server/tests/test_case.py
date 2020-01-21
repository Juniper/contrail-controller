import re
import logging

from builtins import range
from vnc_api import vnc_api
from vnc_api.vnc_api import GlobalSystemConfig
from cfgm_common.tests import test_common


logger = logging.getLogger(__name__)


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

    def assert_vnc_db_doesnt_have_ident(self, test_obj):
        self.assertTill(self.vnc_db_doesnt_have_ident, obj=test_obj)

    def assert_vnc_db_has_ident(self, test_obj):
        self.assertTill(self.vnc_db_has_ident, obj=test_obj)
# end class ApiServerTestCase


class InPlaceConfigTestCase(ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(InPlaceConfigTestCase, cls).setUpClass(*args, **kwargs)
        cls.load_db_contents()

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        cls.dump_db_contents()
        super(InPlaceConfigTestCase, cls).tearDownClass()

    def setUp(self):
        super(InPlaceConfigTestCase, self).setUp()
        gsc_fq_name = GlobalSystemConfig().fq_name
        self.gsc = self.api.global_system_config_read(gsc_fq_name)

    @property
    def api(self):
        return self._vnc_lib

    @staticmethod
    def set_properties(obj, prop_map):
        """Set values to object using properties map.

        For every property which allow for 'Create' operation,
        set a value from prop_map.

        :param obj: schema resource
        :param prop_map: dict
        :return: schema resource
        """
        prop_not_found = []
        for prop, info in obj.prop_field_types.items():
            if 'C' in info['operations']:
                if prop not in prop_map:
                    prop_not_found.append(prop)
                    continue

                set_method = getattr(obj, 'set_%s' % prop)
                set_method(prop_map[prop])
        if len(prop_not_found) > 0:
            raise Exception(
                'Properties nod defined in prop_map: '
                '{} for object: {}'.format(', '.join(prop_not_found),
                                           obj.__class__.__name__))
        return obj

    def assertSchemaObjCreated(self, obj):
        """Create schema object and assert that uuid has been assigned.

        :param obj: schema resource
        """
        # Create and verify that uuid exists
        uuid = self.api.job_template_create(obj)
        self.assertIsNotNone(uuid)
# end class InPlaceConfigTestCase
